#ifndef LEGO_HAL_H
#define LEGO_HAL_H

#include <Arduino.h>

void halRestart(void);
uint8_t halGetHeapFragmentation(void);
String halGetResetInfo(void);
size_t halGetMaxFreeBlock(void);
size_t halGetFreeHeap(void);
String halGetCoreVersion(void);
String halGetChipModel();
String halGetMacAddress(int start, const char * seperator);
uint16_t halGetCpuFreqMHz(void);
String halFormatBytes(size_t bytes);

#endif