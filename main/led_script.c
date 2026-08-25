#include "led_script.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io_virtual.h"
#include "sdmmc_fat.h"
#include "shell.h"

static const char *TAG = "ledscript";

#define LED_SCRIPT_TASK_STACK_BYTES     4096
#define LED_SCRIPT_TASK_PRIORITY        3
#define LED_SCRIPT_MAX_OPS              128
#define LED_SCRIPT_MAX_LINE             96
#define LED_SCRIPT_MAX_NAME             16
#define LED_SCRIPT_WAIT_MAX_MS          60000
#define LED_SCRIPT_WAIT_CHUNK_MS        20
#define LED_SCRIPT_LOOP_MAX             10000

typedef enum {
    LED_OP_SET = 0,
    LED_OP_WAIT,
    LED_OP_LOOP,
    LED_OP_END,
    LED_OP_OFF
} led_op_type_t;

typedef enum {
    LED_SEC_NONE = 0,
    LED_SEC_CAL,
    LED_SEC_SCRIPT,
    LED_SEC_SKIP
} led_section_t;

typedef struct {
    uint8_t  op;
    uint8_t  led;   /* 0 = all, 1..5 */
    uint8_t  r;
    uint8_t  g;
    uint8_t  b;
    uint16_t arg;   /* wait ms, or loop count */
} led_op_t;

typedef struct {
    char     name[LED_SCRIPT_MAX_NAME];
    uint16_t gain_r;
    uint16_t gain_g;
    uint16_t gain_b;
    uint16_t nops;
    led_op_t ops[LED_SCRIPT_MAX_OPS];
} led_script_t;

static led_script_t s_staging;
static led_script_t s_active;
static TaskHandle_t s_task;
static volatile uint8_t s_abort;
static volatile uint8_t s_running;
static char s_err[96];
static char s_path[48];

static int str_ieq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (toupper(ca) != toupper(cb)) {
            return 0;
        }
    }
    return *a == *b;
}

static void set_err(const char *msg)
{
    strncpy(s_err, msg, sizeof(s_err) - 1);
    s_err[sizeof(s_err) - 1] = '\0';
}

static void set_errf_line(int line, const char *msg)
{
    snprintf(s_err, sizeof(s_err), "line %d: %s", line, msg);
}

static char *trim(char *s)
{
    char *end;

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return s;
}

static void strip_comment(char *s)
{
    char *p = strchr(s, '#');
    if (p != NULL) {
        *p = '\0';
    }
}

static uint8_t apply_gain(uint8_t v, uint16_t gain_pct)
{
    uint32_t x = (uint32_t)v * (uint32_t)gain_pct / 100u;
    if (x > 255u) {
        x = 255u;
    }
    return (uint8_t)x;
}

static void rgb_write(uint8_t led, uint8_t r, uint8_t g, uint8_t b)
{
    r = apply_gain(r, s_active.gain_r);
    g = apply_gain(g, s_active.gain_g);
    b = apply_gain(b, s_active.gain_b);
    if (led == 0) {
        uint8_t i;
        for (i = 1; i <= 5; i++) {
            (void)io_virtual_rgb_set(i, r, g, b);
        }
    } else {
        (void)io_virtual_rgb_set(led, r, g, b);
    }
}

static int parse_u16(const char *s, unsigned max_v, uint16_t *out)
{
    char *end = NULL;
    unsigned long v;

    if (s == NULL || *s == '\0') {
        return -1;
    }
    v = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || v > max_v) {
        return -1;
    }
    *out = (uint16_t)v;
    return 0;
}

static int parse_u8_255(const char *s, uint8_t *out)
{
    uint16_t v;
    if (parse_u16(s, 255, &v) != 0) {
        return -1;
    }
    *out = (uint8_t)v;
    return 0;
}

static int split_ws(char *line, char **tok, int max_tok)
{
    int n = 0;

    while (*line != '\0' && n < max_tok) {
        while (*line != '\0' && isspace((unsigned char)*line)) {
            line++;
        }
        if (*line == '\0') {
            break;
        }
        tok[n++] = line;
        while (*line != '\0' && !isspace((unsigned char)*line)) {
            line++;
        }
        if (*line != '\0') {
            *line++ = '\0';
        }
    }
    return n;
}

static int parse_section(const char *s, led_section_t *sec)
{
    if (str_ieq(s, "cal")) {
        *sec = LED_SEC_CAL;
        return 0;
    }
    if (str_ieq(s, "script")) {
        *sec = LED_SEC_SCRIPT;
        return 0;
    }
    *sec = LED_SEC_SKIP;
    return 1;
}

static int parse_kv(led_script_t *sc, const char *key, const char *val)
{
    if (str_ieq(key, "ver")) {
        uint16_t v;
        if (parse_u16(val, 255, &v) != 0) {
            return -1;
        }
        if (v > 1) {
            ESP_LOGW(TAG, "ver=%u newer than firmware v1, trying anyway", (unsigned)v);
        }
        return 0;
    }
    if (str_ieq(key, "name")) {
        strncpy(sc->name, val, sizeof(sc->name) - 1);
        sc->name[sizeof(sc->name) - 1] = '\0';
        return 0;
    }
    if (str_ieq(key, "gain_r")) {
        return parse_u16(val, 200, &sc->gain_r);
    }
    if (str_ieq(key, "gain_g")) {
        return parse_u16(val, 200, &sc->gain_g);
    }
    if (str_ieq(key, "gain_b")) {
        return parse_u16(val, 200, &sc->gain_b);
    }
    ESP_LOGW(TAG, "ignore key '%s'", key);
    return 0;
}

static int parse_cmd(led_script_t *sc, char **tok, int ntok)
{
    led_op_t *op;

    if (sc->nops >= LED_SCRIPT_MAX_OPS) {
        set_err("too many ops (max 128)");
        return -1;
    }
    op = &sc->ops[sc->nops];
    memset(op, 0, sizeof(*op));

    if (str_ieq(tok[0], "set")) {
        uint8_t r, g, b;
        if (ntok != 5) {
            set_err("set wants: set <1-5|all> <r> <g> <b>");
            return -1;
        }
        if (str_ieq(tok[1], "all")) {
            op->led = 0;
        } else {
            uint8_t led;
            if (parse_u8_255(tok[1], &led) != 0 || led < 1 || led > 5) {
                set_err("set lamp must be 1-5 or all");
                return -1;
            }
            op->led = led;
        }
        if (parse_u8_255(tok[2], &r) != 0 ||
            parse_u8_255(tok[3], &g) != 0 ||
            parse_u8_255(tok[4], &b) != 0) {
            set_err("set RGB must be 0-255");
            return -1;
        }
        op->op = LED_OP_SET;
        op->r = r;
        op->g = g;
        op->b = b;
    } else if (str_ieq(tok[0], "wait")) {
        uint16_t ms;
        if (ntok != 2) {
            set_err("wait wants: wait <ms>");
            return -1;
        }
        if (parse_u16(tok[1], LED_SCRIPT_WAIT_MAX_MS, &ms) != 0) {
            set_err("wait ms 0-60000");
            return -1;
        }
        op->op = LED_OP_WAIT;
        op->arg = ms;
    } else if (str_ieq(tok[0], "loop")) {
        uint16_t n;
        if (ntok != 2) {
            set_err("loop wants: loop <n>  (0=forever)");
            return -1;
        }
        if (parse_u16(tok[1], LED_SCRIPT_LOOP_MAX, &n) != 0) {
            set_err("loop n 0-10000");
            return -1;
        }
        op->op = LED_OP_LOOP;
        op->arg = n;
    } else if (str_ieq(tok[0], "end")) {
        if (ntok != 1) {
            set_err("end takes no args");
            return -1;
        }
        op->op = LED_OP_END;
    } else if (str_ieq(tok[0], "off")) {
        if (ntok != 1) {
            set_err("off takes no args");
            return -1;
        }
        op->op = LED_OP_OFF;
    } else {
        snprintf(s_err, sizeof(s_err), "unknown cmd '%s'", tok[0]);
        return -1;
    }

    sc->nops++;
    return 0;
}

static int parse_file(const char *path, led_script_t *sc)
{
    FILE *fp;
    char buf[LED_SCRIPT_MAX_LINE];
    int line_no = 0;
    led_section_t sec = LED_SEC_NONE;

    memset(sc, 0, sizeof(*sc));
    sc->gain_r = 100;
    sc->gain_g = 100;
    sc->gain_b = 100;
    strncpy(sc->name, "-", sizeof(sc->name) - 1);

    fp = fopen(path, "r");
    if (fp == NULL) {
        snprintf(s_err, sizeof(s_err), "open fail: %s", path);
        return -1;
    }

    while (fgets(buf, sizeof(buf), fp) != NULL) {
        char *p;
        size_t len;

        line_no++;
        len = strlen(buf);
        if (strchr(buf, '\n') == NULL && len == sizeof(buf) - 1) {
            int extra = fgetc(fp);
            if (extra != EOF) {
                fclose(fp);
                set_errf_line(line_no, "line too long");
                return -1;
            }
        }

        strip_comment(buf);
        p = trim(buf);
        if (p[0] == '\0') {
            continue;
        }

        if (p[0] == '[') {
            char *rb = strchr(p, ']');
            if (rb == NULL) {
                fclose(fp);
                set_errf_line(line_no, "bad section");
                return -1;
            }
            *rb = '\0';
            p = trim(p + 1);
            if (parse_section(p, &sec) != 0) {
                ESP_LOGW(TAG, "line %d: skip unknown section [%s]", line_no, p);
            }
            continue;
        }

        if (sec == LED_SEC_SKIP) {
            continue;
        }

        if (strchr(p, '=') != NULL && (sec == LED_SEC_NONE || sec == LED_SEC_CAL)) {
            char *eq = strchr(p, '=');
            char *key;
            char *val;
            *eq = '\0';
            key = trim(p);
            val = trim(eq + 1);
            if (key[0] == '\0') {
                fclose(fp);
                set_errf_line(line_no, "empty key");
                return -1;
            }
            if (parse_kv(sc, key, val) != 0) {
                fclose(fp);
                set_errf_line(line_no, "bad key=value");
                return -1;
            }
            continue;
        }

        if (sec == LED_SEC_CAL) {
            fclose(fp);
            set_errf_line(line_no, "command not allowed in [cal]");
            return -1;
        }

        {
            char *tok[8];
            int ntok = split_ws(p, tok, 8);
            if (ntok <= 0) {
                continue;
            }
            if (parse_cmd(sc, tok, ntok) != 0) {
                fclose(fp);
                /* parse_cmd already filled s_err; prefix line if needed */
                if (strncmp(s_err, "line ", 5) != 0) {
                    char tmp[96];
                    strncpy(tmp, s_err, sizeof(tmp) - 1);
                    tmp[sizeof(tmp) - 1] = '\0';
                    set_errf_line(line_no, tmp);
                }
                return -1;
            }
        }
    }

    fclose(fp);

    if (sc->nops == 0) {
        set_err("no script commands");
        return -1;
    }
    return 0;
}

static int wait_abortable(uint32_t ms)
{
    while (ms > 0 && s_abort == 0) {
        uint32_t chunk = (ms > LED_SCRIPT_WAIT_CHUNK_MS) ? LED_SCRIPT_WAIT_CHUNK_MS : ms;
        vTaskDelay(pdMS_TO_TICKS(chunk));
        ms -= chunk;
    }
    return s_abort != 0;
}

static void run_script(void)
{
    uint16_t pc = 0;
    uint16_t loops_done = 0;

    while (s_abort == 0 && pc < s_active.nops) {
        const led_op_t *op = &s_active.ops[pc];

        switch (op->op) {
        case LED_OP_SET:
            rgb_write(op->led, op->r, op->g, op->b);
            pc++;
            break;
        case LED_OP_OFF:
            rgb_write(0, 0, 0, 0);
            pc++;
            break;
        case LED_OP_WAIT:
            if (wait_abortable(op->arg)) {
                return;
            }
            pc++;
            break;
        case LED_OP_LOOP:
            /* Always yield: a script with no wait must not spin the core / flood I2C. */
            if (wait_abortable(LED_SCRIPT_WAIT_CHUNK_MS)) {
                return;
            }
            if (op->arg == 0) {
                pc = 0;
            } else {
                loops_done++;
                if (loops_done >= op->arg) {
                    return;
                }
                pc = 0;
            }
            break;
        case LED_OP_END:
            return;
        default:
            return;
        }
    }
}

static void led_script_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_abort) {
            s_running = 0;
            continue;
        }
        s_running = 1;
        run_script();
        s_running = 0;
    }
}

static int wait_idle(uint32_t timeout_ms)
{
    uint32_t waited = 0;
    while (s_running != 0) {
        if (waited >= timeout_ms) {
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(LED_SCRIPT_WAIT_CHUNK_MS));
        waited += LED_SCRIPT_WAIT_CHUNK_MS;
    }
    return 0;
}

static int resolve_path(const char *in, char *out, size_t out_sz)
{
    if (in == NULL || in[0] == '\0') {
        strncpy(out, LED_SCRIPT_DEFAULT_PATH, out_sz - 1);
        out[out_sz - 1] = '\0';
        return 0;
    }
    if (in[0] == '/') {
        strncpy(out, in, out_sz - 1);
        out[out_sz - 1] = '\0';
        return 0;
    }
    /* Bare 8.3 name → /sdcard/LED/<name>. Path with slash → /sdcard/<path>. */
    if (strchr(in, '/') == NULL) {
        if (snprintf(out, out_sz, "%s/%s", LED_SCRIPT_DIR, in) >= (int)out_sz) {
            set_err("path too long");
            return -1;
        }
        return 0;
    }
    if (snprintf(out, out_sz, "%s/%s", sdmmc_fat_mount_path(), in) >= (int)out_sz) {
        set_err("path too long");
        return -1;
    }
    return 0;
}

esp_err_t led_script_init(void)
{
    s_err[0] = '\0';
    s_path[0] = '\0';
    s_abort = 0;
    s_running = 0;
    if (xTaskCreate(led_script_task, "led_script", LED_SCRIPT_TASK_STACK_BYTES,
                    NULL, LED_SCRIPT_TASK_PRIORITY, &s_task) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "xTaskCreate(led_script) failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t led_script_play(const char *path)
{
    char full[48];

    s_err[0] = '\0';
    if (s_task == NULL) {
        set_err("player not started");
        return ESP_ERR_INVALID_STATE;
    }
    if (!sdmmc_fat_is_mounted()) {
        set_err("sd not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    if (resolve_path(path, full, sizeof(full)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (parse_file(full, &s_staging) != 0) {
        return ESP_FAIL;
    }

    s_abort = 1;
    if (wait_idle(2000) != 0) {
        s_abort = 0;
        set_err("stop timeout");
        return ESP_ERR_TIMEOUT;
    }

    s_active = s_staging;
    strncpy(s_path, full, sizeof(s_path) - 1);
    s_path[sizeof(s_path) - 1] = '\0';
    s_abort = 0;
    xTaskNotifyGive(s_task);
    ESP_LOGI(TAG, "play %s name=%s ops=%u gain=%u/%u/%u",
             s_path, s_active.name, (unsigned)s_active.nops,
             (unsigned)s_active.gain_r, (unsigned)s_active.gain_g,
             (unsigned)s_active.gain_b);
    return ESP_OK;
}

esp_err_t led_script_stop(void)
{
    s_err[0] = '\0';
    if (s_task == NULL) {
        set_err("player not started");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_running == 0) {
        return ESP_OK;
    }
    s_abort = 1;
    if (wait_idle(2000) != 0) {
        set_err("stop timeout");
        return ESP_ERR_TIMEOUT;
    }
    s_abort = 0;
    return ESP_OK;
}

int led_script_is_playing(void)
{
    return s_running != 0;
}

const char *led_script_last_error(void)
{
    return s_err;
}

const char *led_script_path(void)
{
    return s_path[0] != '\0' ? s_path : LED_SCRIPT_DEFAULT_PATH;
}

static int shell_cmd_ledplay(int argc, char *argv[])
{
    SHELL_TypeDef *sh = shellGetCurrent();
    const char *path = NULL;
    esp_err_t err;

    if (argc >= 2) {
        path = argv[1];
    }
    err = led_script_play(path);
    if (sh) {
        if (err == ESP_OK) {
            shellPrint(sh, "play %s name=%s ops=%u\r\n",
                       led_script_path(), s_active.name, (unsigned)s_active.nops);
        } else {
            shellPrint(sh, "ledplay fail: %s\r\n", led_script_last_error());
        }
    }
    return (err == ESP_OK) ? 0 : -1;
}

static int shell_cmd_ledstop(int argc, char *argv[])
{
    SHELL_TypeDef *sh = shellGetCurrent();
    esp_err_t err;

    (void)argc;
    (void)argv;
    err = led_script_stop();
    if (sh) {
        if (err == ESP_OK) {
            shellPrint(sh, "stopped\r\n");
        } else {
            shellPrint(sh, "ledstop fail: %s\r\n", led_script_last_error());
        }
    }
    return (err == ESP_OK) ? 0 : -1;
}

SHELL_EXPORT_CMD_EX(ledplay, shell_cmd_ledplay, "play LED.CFG from SD", ledplay [file]);
SHELL_EXPORT_CMD_EX(ledstop, shell_cmd_ledstop, "stop LED script", ledstop);
