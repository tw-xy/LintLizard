#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app_remote.h"
#include "app_roam.h"

static uint32_t now;
static const char *input = "";
uint32_t BSP_Millis(void) { return now; }
uint8_t Radar_IsOnline(uint32_t time) { (void)time; return 1; }
uint16_t Radar_GetFrontMm(void) { return 2000; }
uint16_t Radar_GetLeftMm(void) { return 2000; }
uint16_t Radar_GetRightMm(void) { return 2000; }
uint16_t ESP_Read(uint8_t *dst, uint16_t max)
{
    unsigned n = 0;
    while(*input && n < max) dst[n++] = (uint8_t)*input++;
    return (uint16_t)n;
}
static void frame(const char *line, int16_t expectedX, int16_t expectedY)
{
    int16_t x, y;
    input = line;
    Remote_Poll();
    Roam_Update(now, Remote_Get(), &x, &y);
    assert(x == expectedX && y == expectedY);
}
int main(void)
{
    int16_t x, y;
    Remote_Init(); Roam_Init();
    frame("{\"x\":0,\"y\":100,\"manual\":1}\n", 0, 100);
    frame("{\"x\":0,\"y\":-100,\"manual\":1}\n", 0, -100);
    frame("{\"x\":-50,\"y\":50,\"manual\":1}\n", -50, 50);
    frame("{\"x\":0,\"y\":0,\"manual\":1}\n", 0, 0);
    now = 2000;
    frame("{\"x\":0,\"y\":0,\"manual\":1}\n", 0, 0);
    assert(Roam_GetState() == ROAM_IDLE);
    frame("{\"x\":0,\"y\":50,\"manual\":1}\n", 0, 50);
    now += 301;
    Remote_Poll();
    Roam_Update(now, Remote_Get(), &x, &y);
    assert(!Remote_Get()->online && x == 0 && y == 0);

    /* Legacy frames still use the old roam/disarm gestures. */
    frame("{\"x\":0,\"y\":-100}\n", 0, 0);
    assert(!Remote_Get()->manual);
    frame("{\"x\":0,\"y\":50}\n", 0, 50);
    frame("{\"x\":0,\"y\":0}\n", 0, 0);
    now += 1600;
    input = "{\"x\":0,\"y\":0}\n";
    Remote_Poll();
    Roam_Update(now, Remote_Get(), &x, &y);
    assert(y > 0 && Roam_GetState() == ROAM_FWD);
    frame("{\"x\":0,\"y\":0,\"manual\":1}\n", 0, 0);
    now += 2000;
    frame("{\"x\":0,\"y\":0}\n", 0, 0);
    assert(Roam_GetState() == ROAM_IDLE);
    puts("manual control: forward/reverse, release, timeout and legacy roam OK");
}
