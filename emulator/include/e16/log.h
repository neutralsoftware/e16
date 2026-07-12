#ifndef E16_LOG_H
#define E16_LOG_H

#include <string>

namespace e16 {

void initializeLog();
void logInfo(const std::string &message);
void logError(const std::string &message);
const std::string &logFilePath();

}

#endif
