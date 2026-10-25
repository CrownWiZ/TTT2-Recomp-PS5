#include "setup/mobile_install.h"
#include <jni.h>
extern "C" JNIEXPORT jstring JNICALL
Java_org_ttt2_setup_SetupActivity_extract(JNIEnv* env, jclass, jint iso, jint tu, jstring target, jobject callback) {
    const char* utf = env->GetStringUTFChars(target, nullptr);
    if (!utf) return nullptr;
    std::filesystem::path destination(utf);
    env->ReleaseStringUTFChars(target, utf);
    auto cls = env->GetObjectClass(callback);
    auto method = env->GetMethodID(cls, "progress", "(ILjava/lang/String;)Z");
    if (!method) return nullptr;
    std::string error;
    bool ok = ttt2::setup::InstallFromDescriptors(iso, tu, destination, [&](int percent, const std::string& message) {
        jstring text = env->NewStringUTF(message.c_str());
        if (!text) return false;
        bool keepGoing = env->CallBooleanMethod(callback, method, percent, text);
        env->DeleteLocalRef(text);
        return !env->ExceptionCheck() && keepGoing;
    }, error);
    if (env->ExceptionCheck()) return nullptr;
    return ok ? nullptr : env->NewStringUTF(error.c_str());
}
