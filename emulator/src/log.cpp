#include "e16/log.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace e16 {

namespace {
std::ofstream output;
std::mutex outputMutex;
std::string outputPath;

#ifndef _WIN32
void crashSignal(int number) {
    char message[] = "fatal signal 000\n";
    int position = 15;
    int value = number;
    do {
        message[position--] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value > 0 && position >= 13);
    int descriptor = open("/tmp/e16.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (descriptor >= 0) {
        write(descriptor, message, sizeof(message) - 1);
        close(descriptor);
    }
    std::signal(number, SIG_DFL);
    kill(getpid(), number);
}
#endif

std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream text;
    text << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return text.str();
}

void write(const std::string &level, const std::string &message,
           bool stderrOutput) {
    std::lock_guard<std::mutex> lock(outputMutex);
    std::string line = timestamp() + " [" + level + "] " + message;
    if (output) {
        output << line << '\n';
        output.flush();
    }
    if (stderrOutput) {
        std::cerr << line << '\n';
    }
}
}

void initializeLog() {
    std::lock_guard<std::mutex> lock(outputMutex);
    std::error_code error;
    std::filesystem::path directory =
        std::filesystem::temp_directory_path(error);
    if (error) {
        directory = "/tmp";
    }
    outputPath = (directory / "e16.log").string();
    output.open(outputPath, std::ios::out | std::ios::trunc);
#ifndef _WIN32
    std::signal(SIGABRT, crashSignal);
    std::signal(SIGBUS, crashSignal);
    std::signal(SIGFPE, crashSignal);
    std::signal(SIGILL, crashSignal);
    std::signal(SIGSEGV, crashSignal);
#endif
    std::set_terminate([] {
        std::string message = "unhandled fatal exception";
        if (std::exception_ptr exception = std::current_exception()) {
            try {
                std::rethrow_exception(exception);
            } catch (const std::exception &error) {
                message += ": ";
                message += error.what();
            } catch (...) {
            }
        }
        logError(message);
        std::_Exit(1);
    });
}

void logInfo(const std::string &message) { write("INFO", message, false); }

void logError(const std::string &message) { write("ERROR", message, true); }

const std::string &logFilePath() { return outputPath; }

}
