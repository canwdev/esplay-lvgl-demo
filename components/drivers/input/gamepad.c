#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "gamepad.h"
#include <driver/adc.h>
#include "esp_adc_cal.h"
#if CONFIG_USE_LVGL
#include "lvgl/lv_hal/lv_hal_indev.h"
#include "lvgl/lv_core/lv_group.h"
#endif

#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define WRITE_BIT I2C_MASTER_WRITE  /*!< I2C master write */
#define READ_BIT I2C_MASTER_READ    /*!< I2C master read */
#define ACK_CHECK_EN 0x1            /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0           /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                 /*!< I2C ack value */
#define NACK_VAL 0x1                /*!< I2C nack value */

static volatile bool input_task_is_running = false;
static volatile input_gamepad_state gamepad_state;
static input_gamepad_state previous_gamepad_state;
static uint8_t debounce[GAMEPAD_INPUT_MAX];
static volatile bool input_gamepad_initialized = false;
static SemaphoreHandle_t xSemaphore;
static gpio_num_t i2c_gpio_sda = 21;
static gpio_num_t i2c_gpio_scl = 22;
static uint32_t i2c_frequency = 100000;
static i2c_port_t i2c_port = I2C_NUM_0;

static esp_err_t i2c_master_driver_initialize()
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = i2c_gpio_sda,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = i2c_gpio_scl,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = i2c_frequency
    };
    return i2c_param_config(i2c_port, &conf);
}

static uint8_t i2c_keypad_read()
{
    int len = 1;
    uint8_t *data = malloc(len);

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, 0x20 << 1 | READ_BIT, ACK_CHECK_EN);
    i2c_master_read_byte(cmd, data + len - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    uint8_t val = data[0];
    free(data);

    return val;
}

input_gamepad_state gamepad_input_read_raw()
{
    input_gamepad_state state = {0};

    uint8_t i2c_data = i2c_keypad_read();
    for (int i = 0; i < 8; ++i)
    {
        if(((1<<i)&i2c_data) == 0)
            state.values[i] = 1;
        else
            state.values[i] = 0;
    }

    state.values[GAMEPAD_INPUT_MENU] = !(gpio_get_level(MENU));
    state.values[GAMEPAD_INPUT_L] = !(gpio_get_level(L_BTN));
    state.values[GAMEPAD_INPUT_R] = !(gpio_get_level(R_BTN));

    return state;
}

static void input_task(void *arg)
{
    input_task_is_running = true;

    // Initialize state
    for (int i = 0; i < GAMEPAD_INPUT_MAX; ++i)
    {
        debounce[i] = 0xff;
    }

    while (input_task_is_running)
    {
        // Shift current values
        for (int i = 0; i < GAMEPAD_INPUT_MAX; ++i)
        {
            debounce[i] <<= 1;
        }

        // Read hardware
        input_gamepad_state state = gamepad_input_read_raw();

        // Debounce
        xSemaphoreTake(xSemaphore, portMAX_DELAY);

        for (int i = 0; i < GAMEPAD_INPUT_MAX; ++i)
        {
            debounce[i] |= state.values[i] ? 1 : 0;
            uint8_t val = debounce[i] & 0x03; //0x0f;
            switch (val)
            {
            case 0x00:
                gamepad_state.values[i] = 0;
                break;

            case 0x03: //0x0f:
                gamepad_state.values[i] = 1;
                break;

            default:
                // ignore
                break;
            }
        }

        previous_gamepad_state = gamepad_state;

        xSemaphoreGive(xSemaphore);

        // delay
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    input_gamepad_initialized = false;

    vSemaphoreDelete(xSemaphore);

    // Remove the task from scheduler
    vTaskDelete(NULL);

    // Never return
    while (1)
    {
        vTaskDelay(1);
    }
}

void gamepad_read(input_gamepad_state *out_state)
{
    if (!input_gamepad_initialized)
        abort();


    if (gamepad_state.values[GAMEPAD_INPUT_UP] == 1)
    {
        printf("UP\n");
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_DOWN] == 1)
    {
        printf("DOWN\n");
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_LEFT] == 1)
    {
        printf("LEFT\n");
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_RIGHT] == 1)
    {
        printf("RIGHT\n");
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_B] == 1)
    {
        printf("ESC\n");
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_A] == 1)
    {
        printf("ENTER\n");
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_SELECT] == 1)
    {
        printf("SELECT\n");
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_START] == 1)
    {
        printf("START\n");
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_MENU] == 1)
    {
        printf("MENU\n");
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_L] == 1)
    {
        printf("L\n");
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_R] == 1)
    {
        printf("R\n");
    }

    xSemaphoreTake(xSemaphore, portMAX_DELAY);

    *out_state = gamepad_state;

    // printf("gamepad_state: %d\n", gamepad_state);
    xSemaphoreGive(xSemaphore);
}

void gamepad_init()
{
    xSemaphore = xSemaphoreCreateMutex();

    if (xSemaphore == NULL)
    {
        printf("xSemaphoreCreateMutex failed.\n");
        abort();
    }

    //Configure button
    gpio_config_t btn_config;
    btn_config.intr_type = GPIO_INTR_ANYEDGE; //Enable interrupt on both rising and falling edges
    btn_config.mode = GPIO_MODE_INPUT;        //Set as Input

    i2c_master_driver_initialize();
    i2c_driver_install(i2c_port, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);

    btn_config.pin_bit_mask = (uint64_t)      //Bitmask
                              ((uint64_t)1 << L_BTN) |
                              ((uint64_t)1 << R_BTN);

    btn_config.pull_up_en = GPIO_PULLUP_ENABLE;      //Disable pullup
    btn_config.pull_down_en = GPIO_PULLDOWN_DISABLE; //Enable pulldown
    gpio_config(&btn_config);

    gpio_set_direction(MENU, GPIO_MODE_INPUT);

    input_gamepad_initialized = true;

    // Start background polling
    xTaskCreatePinnedToCore(&input_task, "input_task", 1024 * 2, NULL, 5, NULL, 1);

    printf("input_gamepad_init done.\n");
}



void input_gamepad_terminate()
{
    if (!input_gamepad_initialized)
        abort();

    i2c_driver_delete(i2c_port);
    input_task_is_running = false;
}

#if CONFIG_USE_LVGL
bool lv_keypad_read(lv_indev_data_t *data)
{
    if (!input_gamepad_initialized)
        abort();

    if (gamepad_state.values[GAMEPAD_INPUT_UP] == 1)
    {
        printf("UP\n");
        data->state = LV_INDEV_STATE_PR;
        data->key = LV_GROUP_KEY_UP;
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_DOWN] == 1)
    {
        printf("DOWN\n");
        data->state = LV_INDEV_STATE_PR;
        data->key = LV_GROUP_KEY_DOWN;
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_LEFT] == 1)
    {
        printf("LEFT\n");
        data->state = LV_INDEV_STATE_PR;
        data->key = LV_GROUP_KEY_LEFT;
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_RIGHT] == 1)
    {
        printf("RIGHT\n");
        data->state = LV_INDEV_STATE_PR;
        data->key = LV_GROUP_KEY_RIGHT;
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_B] == 1)
    {
        printf("ESC\n");
        data->state = LV_INDEV_STATE_PR;
        data->key = LV_GROUP_KEY_ESC;
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_A] == 1)
    {
        printf("ENTER\n");
        data->state = LV_INDEV_STATE_PR;
        data->key = LV_GROUP_KEY_ENTER;
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_SELECT] == 1)
    {
        printf("SELECT\n");
        data->state = LV_INDEV_STATE_PR;
        data->key = LV_GROUP_KEY_NEXT;
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_START] == 1)
    {
        printf("START\n");
        data->state = LV_INDEV_STATE_PR;
        data->key = LV_GROUP_KEY_PREV;
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
    }

    return false;
}
#endif
