#include "frame_decoder.h"

String decodeMessage(uint8_t statusByte, uint8_t errorByte, bool &isCriticalError)
{
  String msg;
  const uint8_t doorState = statusByte >> 4;

  switch (doorState)
  {
  case 0:
    msg += "🚪 Door Opening";
    break;
  case 1:
    msg += "🚪 Door Open";
    break;
  case 2:
    msg += "🚪 Door Half Open";
    break;
  case 3:
    msg += "🚪 Door Closing";
    break;
  case 4:
    msg += "✅ Door Closed";
    break;
  default:
    msg += "❓ Unknown Door";
    break;
  }

  // Bit 0 reports the lamp state; the upper nibble reports the door state.
  if (statusByte & 0x01)
    msg += "\n💡 Light ON";
  else
    msg += "\n💡 Light OFF";
  isCriticalError = false;

  switch (errorByte)
  {
  case 0x00:
    msg += "\n✅ No Error";
    break;
  case 0x01:
    msg += "\n⚠️ Motor Timeout";
    isCriticalError = true;
    break;
  case 0x02:
    msg += "\n⚠️ Over Force (F)";
    isCriticalError = true;
    break;
  case 0x03:
    msg += "\n⚠️ IR Blocked (b)";
    isCriticalError = true;
    break;
  case 0x04:
    msg += "\n⚠️ Remote Disabled";
    isCriticalError = true;
    break;
  case 0x05:
    msg += "\n⚠️ Wall Console Short";
    isCriticalError = true;
    break;
  case 0x06:
    msg += "\n🚨 Motor Sensor No Signal (dE)";
    break;
  case 0x07:
    msg += "\n🚨 IR Fault (b)";
    break;
  }

  return msg;
}
