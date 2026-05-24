#ifndef SD_CARD_H
#define SD_CARD_H

#include <Arduino.h>

namespace appfw {

bool mountSdCard(const char *reason);
void unmountSdCard(const char *reason);
bool isSdCardMounted();

}  // namespace appfw

#endif
