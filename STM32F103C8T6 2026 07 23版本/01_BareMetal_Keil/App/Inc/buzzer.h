#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>
#include <stdint.h>

void buzzer_set(bool on);
void buzzer_pulse_start(uint32_t duration_ms, uint32_t now_ms);
void buzzer_update(uint32_t now_ms);
bool buzzer_active(void);

#endif
