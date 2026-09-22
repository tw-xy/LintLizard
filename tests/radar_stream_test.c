/* Host test for the real radar parser. No board or motor I/O.
 * gcc -std=c99 -Wall -Wextra -IApp -IBsp tests/radar_stream_test.c \
 *     App/app_radar.c -o /tmp/radar_stream_test && /tmp/radar_stream_test
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app_radar.h"

static uint8_t packet[170];
static unsigned remaining, position, calls, accepted;
static uint8_t full;

uint32_t BSP_Millis(void) { return 0; }
uint32_t RADAR_RxOverflow(void) { return 0; }
void DBG_Printf(const char *fmt, ...) { (void)fmt; }

uint16_t RADAR_Read(uint8_t *dst, uint16_t maxLen)
{
    unsigned count = remaining < maxLen ? remaining : maxLen;
    memcpy(dst, packet + position, count);
    position += count;
    remaining -= count;
    return (uint16_t)count;
}

uint8_t ESP_TryWriteFrame(const uint8_t *src, uint16_t len)
{
    const char expected[] = "R,35320,1000\n";
    calls++;
    assert(len == sizeof(expected) - 1);
    assert(memcmp(src, expected, len) == 0);
    if(full) return 0;
    accepted++;
    return 1;
}

static void write16(unsigned offset, uint16_t value)
{
    packet[offset] = (uint8_t)value;
    packet[offset + 1] = (uint8_t)(value >> 8);
}

static void prepare(unsigned count, uint16_t raw)
{
    uint16_t checksum = 0;
    unsigned i;
    memset(packet, 0, sizeof(packet));
    packet[0] = 0xAA;
    packet[1] = 0x55;
    packet[3] = (uint8_t)count;
    write16(4, 1); /* start/end = 0 degrees + mandatory check bit */
    write16(6, 1);
    for(i = 0; i < count; i++) write16(8 + 2*i, raw);
    for(i = 0; i < 8 + 2*count; i += 2)
        checksum ^= (uint16_t)(packet[i] | ((uint16_t)packet[i+1] << 8));
    write16(8 + 2*count, checksum);
    position = 0;
    remaining = 10 + 2*count;
}

int main(void)
{
    Radar_Init();
    prepare(80, 4000);
    Radar_Task(10);
    assert(calls == 10 && accepted == 10);
    assert(Radar_GetPackets() == 1);
    assert(Radar_GetFrontMm() == 1000);

    prepare(80, 4000);
    packet[168] ^= 1;
    Radar_Task(20);
    assert(calls == 10 && Radar_GetChecksumErrors() == 1);

    /* Backpressure must not prevent parsing or refreshing avoidance. */
    full = 1;
    prepare(80, 4000);
    Radar_Task(30);
    assert(calls == 20 && accepted == 10);
    assert(Radar_GetPackets() == 2 && Radar_IsOnline(30));
    assert(Radar_GetFrontMm() == 1000);
    full = 0;

    /* Decimation phase persists across packet boundaries. */
    prepare(7, 4000);
    Radar_Task(40);
    assert(calls == 20);
    prepare(1, 4000);
    Radar_Task(50);
    assert(calls == 21 && accepted == 11);

    prepare(80, 0);
    Radar_Task(60);
    assert(calls == 21);
    prepare(80, 36000); /* beyond 8m */
    Radar_Task(70);
    assert(calls == 21);
    assert(!Radar_IsOnline(400));
    puts("radar stream: checksum, decimation, backpressure, avoidance and expiry OK");
    return 0;
}
