#ifndef FRAME_DECODER_H
#define FRAME_DECODER_H

#include <Arduino.h>

String decodeMessage(uint8_t statusByte,
                     uint8_t errorByte,
                     bool &isCriticalError);

#endif