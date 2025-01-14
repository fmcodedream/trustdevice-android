//
// Created by zuchangwu on 2023/4/10.
//

#include "detection_risk.h"
#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include "utils.h"

extern "C" JNIEXPORT int JNICALL readTracePid(const char *file_path) {
    int tracerPid = 0;
    std::ifstream status_file(file_path);
    if (!status_file) {
        return tracerPid;
    }
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.find("TracerPid:") == 0) {
            tracerPid = std::stoi(line.substr(11));
            break;
        }
    }
    status_file.close();
    return tracerPid;
}

extern "C" JNIEXPORT bool JNICALL detectTracePid() {
    const char *status_path = "/proc/self/status";
    int tracerPid = readTracePid(status_path);
    if (tracerPid != 0) {
        return true;
    }
    return false;
}

extern "C" JNIEXPORT bool JNICALL detectTaskTracerPid() {
    pid_t pid = getpid();
    char buffer[512];
    const char *format = "/proc/%d/task/%d/status";
    std::sprintf(buffer, format, pid, pid);
    int tracerPid = readTracePid(buffer);
    if (tracerPid != 0) {
        return true;
    }
    return false;
}

extern "C" JNIEXPORT jint JNICALL detect_debug(JNIEnv *env, jclass clazz) {
    int result = 0;
    if (detectTracePid()) {
        result |= 0x1 << 1;
    }
    if (detectTaskTracerPid()) {
        result |= 0x1 << 2;
    }
    return static_cast<jint>(result);
}

extern "C" JNIEXPORT size_t JNICALL detect_frida(char *hook_method, const size_t max_length) {
    char *libc_method_names[] = {"strcat", "fopen", "open", "read", "strcmp", "strstr", "fgets",
                                 "access", "ptrace", "__system_property_get"};
    char *libc_so_path;
#ifdef __LP64__
    libc_so_path = "/system/lib64/libc.so";
#else
    libc_so_path = "/system/lib/libc.so";
#endif
    void *handler = dlopen(libc_so_path, RTLD_NOW);
    if (!handler) {
        return 0;
    }
    for (char *method_name: libc_method_names) {
        void *method_sym = dlsym(handler, method_name);
        if (method_sym == nullptr) {
            continue;
        }
        auto *bytecode = reinterpret_cast<uintptr_t *>(method_sym);
        operation_type operation = *bytecode;
        if (operation == trampoline_code) {
            size_t remaining_length = max_length - strlen(method_name);
            snprintf(hook_method + strlen(hook_method), remaining_length, "%s,", method_name);
        }
    }
    dlclose(handler);
    void *td_method_names[] = {(void *) detect_debug, (void *) detectTaskTracerPid,
                               (void *) detectTracePid, (void *) readTracePid, (void *) detect_hook,
                               (void *) detect_frida};
    size_t td_method_len = sizeof(td_method_names) / sizeof(td_method_names[0]);
    for (size_t i = 0; i < td_method_len; ++i) {
        auto *bytecode = reinterpret_cast<uintptr_t *>(td_method_names[i]);
        operation_type operation = *bytecode;
        if (operation == trampoline_code) {
            std::string str = std::to_string(i);
            size_t remaining_length = max_length - str.size();
            snprintf(hook_method + strlen(hook_method), remaining_length, "%d,", i);
        }
    }
    size_t str_len = strlen(hook_method);
    if (str_len > 0 && hook_method[str_len - 1] == ',') {
        hook_method[str_len - 1] = '\0';
    }
//    LOGD("frida hook method = %s\n", hook_method);
    return str_len;
}

extern "C" JNIEXPORT jstring JNICALL detect_hook(JNIEnv *env, jclass clazz) {
    const size_t max_length = 512;
    char frida_hook_method[max_length] = {};
    detect_frida(frida_hook_method, max_length);
    return env->NewStringUTF(frida_hook_method);
}