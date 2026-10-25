// Build the runtime's shadercache from expanded game archives on Linux or Android.
// No game files or generated shaders belong in the source tree.
//
// This translation unit includes the CPU GPU frontend so it uses the same Xenos
// decoder and HLSL generator as the game. Unused drawing functions are removed
// by --gc-sections; no graphics device or game process is started.
#include "../src/runtime/kernel/gpu_render.cpp"
#include <vkd3d_shader.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace fs = std::filesystem;

struct ArchiveShader {
    bool vertex;
    std::vector<uint32_t> code;
    std::string ucodeName;
};
struct CompileJob {
    std::string source;
    std::string bytecodeName;
    bool vertex;
};

static uint32_t Be32(const uint8_t* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

static uint64_t Hash(const void* bytes, size_t size, uint64_t seed)
{
    const auto* p = static_cast<const uint8_t*>(bytes);
    for (size_t i = 0; i < size; ++i) seed = (seed ^ p[i]) * 1099511628211ull;
    return seed;
}

static std::string Name(const char* kind, uint64_t key, const char* extension)
{
    char name[64];
    std::snprintf(name, sizeof(name), "%s_%016llX.%s", kind, (unsigned long long)key, extension);
    return name;
}

static bool WriteFile(const fs::path& path, const void* bytes, size_t size)
{
    const fs::path temporary = path.string() + ".tmp." + std::to_string(getpid()) + "." +
        std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    {
        std::ofstream file(temporary, std::ios::binary);
        file.write(static_cast<const char*>(bytes), size);
        file.close();
        if (!file) { fs::remove(temporary); return false; }
    }
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) fs::remove(temporary);
    return !error;
}

static bool ValidDxbc(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    char magic[4]{};
    return bool(file.read(magic, sizeof(magic))) && std::memcmp(magic, "DXBC", sizeof(magic)) == 0;
}

static bool ScanArchive(const fs::path& path, std::vector<ArchiveShader>& programs,
                        std::unordered_set<std::string>& seen, const std::function<bool()>& keepGoing)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) { std::perror(path.c_str()); return false; }
    struct stat statbuf{};
    if (fstat(fd, &statbuf) || statbuf.st_size <= 0) { close(fd); return false; }
    const size_t length = size_t(statbuf.st_size);
    auto* bytes = static_cast<const uint8_t*>(mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0));
    if (bytes == MAP_FAILED) { std::perror("mmap"); close(fd); return false; }
    madvise(const_cast<uint8_t*>(bytes), length, MADV_SEQUENTIAL);
    const auto* p = bytes;
    const auto* end = bytes + length;
    uint64_t containers = 0;
    while (size_t(end - p) >= 36)
    {
        if (!keepGoing()) { munmap(const_cast<uint8_t*>(bytes), length); close(fd); return false; }
        p = static_cast<const uint8_t*>(std::memchr(p, 0x10, end - p));
        if (!p || size_t(end - p) < 36) break;
        if (p[1] != 0x2a || p[2] != 0x11 || p[3] > 1) { ++p; continue; }
        const uint32_t virt = Be32(p + 4), phys = Be32(p + 8), shader = Be32(p + 24);
        const uint32_t total = virt + phys;
        if (Be32(p + 28) || Be32(p + 32) || virt < 36 || total < virt || total > 0x40000 ||
            size_t(end - p) < total || shader > virt - 24) { p += 4; continue; }
        const uint32_t offset = Be32(p + shader), size = Be32(p + shader + 4);
        if (!size || size > 8192 * 4 || size % 12 || offset > phys || size != phys - offset) { p += 4; continue; }
        ++containers;
        ArchiveShader program;
        program.vertex = p[3] != 0;
        program.code.reserve(size / 4);
        for (uint32_t i = 0; i < size; i += 4)
            program.code.push_back(Be32(p + virt + offset + i));
        const uint64_t key = Hash(program.code.data(), size,
                                  (program.vertex ? 0x5eedull : 0xc0ffeeull) + program.code.size());
        program.ucodeName = Name(program.vertex ? "vs" : "ps", key, "bin");
        if (seen.insert(program.ucodeName).second) programs.emplace_back(std::move(program));
        p += total;
    }
    std::cout << path.filename().string() << ": " << containers << " containers, "
              << programs.size() << " distinct programs so far\n";
    munmap(const_cast<uint8_t*>(bytes), length);
    close(fd);
    return true;
}

static bool Compile(const CompileJob& job, const fs::path& output, std::string& error)
{
    const vkd3d_shader_hlsl_source_info hlsl{
        VKD3D_SHADER_STRUCTURE_TYPE_HLSL_SOURCE_INFO, nullptr, "main", {},
        job.vertex ? "vs_5_0" : "ps_5_0"};
    vkd3d_shader_compile_info info{};
    info.type = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;
    info.next = &hlsl;
    info.source = {job.source.data(), job.source.size()};
    info.source_type = VKD3D_SHADER_SOURCE_HLSL;
    info.target_type = VKD3D_SHADER_TARGET_DXBC_TPF;
    info.log_level = VKD3D_SHADER_LOG_WARNING;
    info.source_name = job.vertex ? "ttt2 translated vs" : "ttt2 translated ps";
    vkd3d_shader_code code{};
    char* messages = nullptr;
    const int result = vkd3d_shader_compile(&info, &code, &messages);
    if (result || code.size < 4 || std::memcmp(code.code, "DXBC", 4))
        error = messages ? messages : "vkd3d-shader did not produce DXBC";
    if (messages) vkd3d_shader_free_messages(messages);
    bool written = error.empty() && WriteFile(output / job.bytecodeName, code.code, code.size);
    if (error.empty() && !written) error = "cannot write DXBC";
    if (code.code) vkd3d_shader_free_shader_code(&code);
    return written;
}

// Shared by the Linux CLI and Android's CPU-only setup library.
static int PrepareShaders(const fs::path& output, int workers,
                          const std::vector<fs::path>& archives,
                          const std::function<bool(int, const std::string&)>& progress)
{
    std::error_code ec;
    fs::create_directories(output / "ucode", ec);
    if (ec) throw std::runtime_error("Cannot create shader cache: " + ec.message());
    std::vector<ArchiveShader> programs;
    std::unordered_set<std::string> seenMicrocode;
    for (size_t i = 0; i < archives.size(); ++i) {
        const std::string message = "Scanning " + archives[i].filename().string() + " (" +
            std::to_string(i + 1) + "/" + std::to_string(archives.size()) + ")";
        if (!progress(int(i * 20 / archives.size()), message)) return 2;
        // Poll cancellation without flooding the UI while scanning large archives.
        auto nextPoll = std::chrono::steady_clock::now();
        if (!ScanArchive(archives[i], programs, seenMicrocode, [&] {
            auto now = std::chrono::steady_clock::now();
            if (now < nextPoll) return true;
            nextPoll = now + std::chrono::milliseconds(250);
            return progress(int(i * 20 / archives.size()), message);
        })) throw std::runtime_error("Archive scan stopped: " + archives[i].filename().string());
    }
    if (programs.empty()) throw std::runtime_error("No game shaders found in the installed archives");
    std::vector<CompileJob> jobs;
    std::unordered_set<std::string> seenBytecode;
    size_t cached = 0, unsupported = 0;
    size_t translatedCount = 0;
    for (const auto& program : programs)
    {
        ++translatedCount;
        if (!progress(20 + int(translatedCount * 20 / programs.size()),
            "Preparing shader " + std::to_string(translatedCount) + "/" + std::to_string(programs.size()))) return 2;
        std::vector<std::string> sources;
        if (program.vertex)
        {
#if defined(TTT2_ANDROID_SHADER_SETUP)
            auto translated = ttt2_hw::TranslateVertexShader(program.code, true);
#else
            auto translated = ttt2_hw::TranslateVertexShader(program.code);
#endif
            if (!translated.unsupported.empty())
            {
                ++unsupported;
                std::cerr << "UNSUPPORTED " << program.ucodeName << ": " << translated.unsupported << '\n';
                continue;
            }
            sources.push_back(ttt2_hw::VertexShaderHlsl(translated));
        }
        else
        {
            auto translated = ttt2_hw::TranslatePixelShader(program.code);
            if (!translated.unsupported.empty())
            {
                ++unsupported;
                std::cerr << "UNSUPPORTED " << program.ucodeName << ": " << translated.unsupported << '\n';
                continue;
            }
            sources.push_back(ttt2_hw::PixelShaderHlsl(translated));
            if (ttt2_hw::HasTextureStateVariants(translated))
            {
                sources.push_back(ttt2_hw::PixelShaderHlsl(translated, ttt2_hw::TextureStateVariant::Depth));
                sources.push_back(ttt2_hw::PixelShaderHlsl(translated, ttt2_hw::TextureStateVariant::ResolvedColor));
            }
        }
        if (!fs::exists(output / "ucode" / program.ucodeName))
            if (!WriteFile(output / "ucode" / program.ucodeName, program.code.data(), program.code.size() * 4))
                { throw std::runtime_error("Cannot write " + program.ucodeName); }
        for (auto& source : sources)
        {
            const uint64_t textKey = Hash(source.data(), source.size(), 1469598103934665603ull);
            const std::string name = Name(program.vertex ? "vg" : "ps", textKey, "dxbc");
            if (!seenBytecode.insert(name).second) continue;
            if (ValidDxbc(output / name)) { ++cached; continue; }
            // The runtime also accepts the previous vertex-cache prefix, so an
            // upgrade does not need to recompile valid bytecode already on disk.
            if (program.vertex && ValidDxbc(output / Name("vs", textKey, "dxbc"))) { ++cached; continue; }
            jobs.push_back({std::move(source), name, program.vertex});
        }
    }
    std::cout << "Programs=" << programs.size() << " unsupported=" << unsupported
              << " existing_DXBC=" << cached << " to_compile=" << jobs.size()
              << " workers=" << workers << std::endl;
    std::atomic<size_t> next{0}, completed{0}, failed{0};
    std::atomic<bool> cancelled{false};
    std::mutex logMutex;
    std::vector<std::jthread> threads;
    for (int i = 0; i < workers; ++i)
        threads.emplace_back([&] {
            while (!cancelled.load())
            {
                const size_t index = next.fetch_add(1);
                if (index >= jobs.size()) break;
                std::string error;
                bool success = false;
                try { success = Compile(jobs[index], output, error); }
                catch (const std::exception& e) { error = e.what(); }
                if (!success)
                {
                    ++failed;
                    std::lock_guard lock(logMutex);
                    std::cerr << "FAIL " << jobs[index].bytecodeName << ": " << error.substr(0, 500) << '\n';
                }
                const size_t done = ++completed;
                if (done % 100 == 0 || done == jobs.size())
                {
                    std::lock_guard lock(logMutex);
                    std::cout << "Compiled " << done << '/' << jobs.size() << " (failed=" << failed << ")" << std::endl;
                }
            }
        });
    while (completed.load() < jobs.size()) {
        if (!progress(40 + int(completed.load() * 60 / jobs.size()),
            "Compiling shaders: " + std::to_string(completed.load()) + "/" + std::to_string(jobs.size()) +
            " (" + std::to_string(cached) + " already cached)")) {
            cancelled = true; break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    for (auto& thread : threads) thread.join();
    if (cancelled) return 2;
    std::cout << "Done: " << completed - failed << " compiled, " << failed << " failed\n";
    progress(100, "Shaders prepared: " + std::to_string(completed - failed) + " compiled, " +
        std::to_string(cached) + " reused, " + std::to_string(unsupported) + " unsupported, " +
        std::to_string(failed.load()) + " failed. First use of a scene may still prepare GPU pipelines.");
    return failed ? 1 : 0;
}

#if defined(TTT2_ANDROID_SHADER_SETUP)
#include <jni.h>
extern "C" JNIEXPORT jstring JNICALL
Java_org_ttt2_setup_SetupActivity_prepareShaders(JNIEnv* env, jclass, jstring root, jobject callback)
{
    const char* utf = env->GetStringUTFChars(root, nullptr);
    if (!utf) return nullptr;
    const fs::path data(utf);
    env->ReleaseStringUTFChars(root, utf);
    auto cls = env->GetObjectClass(callback);
    auto method = env->GetMethodID(cls, "progress", "(ILjava/lang/String;)Z");
    if (!method) return nullptr;
    try {
        const auto game = fs::exists(data / "installed-game" / "installed.txt") ? data / "installed-game" : data;
        std::vector<fs::path> archives;
        for (const auto& entry : fs::recursive_directory_iterator(game / "game_files_x")) {
            auto name = entry.path().filename().string();
            if (entry.is_regular_file() && name.size() == 11 && name.starts_with("data") &&
                name.ends_with(".bin") && name.find_first_not_of("0123456789", 4) == 7)
                archives.push_back(entry.path());
        }
        std::sort(archives.begin(), archives.end());
        if (archives.empty()) throw std::runtime_error("No game archives found");
        int result = PrepareShaders(data / "cache" / "shadercache", 2, archives,
            [&](int percent, const std::string& message) {
                auto text = env->NewStringUTF(message.c_str());
                if (!text) return false;
                bool keepGoing = env->CallBooleanMethod(callback, method, percent, text);
                env->DeleteLocalRef(text);
                return !env->ExceptionCheck() && keepGoing;
            });
        if (env->ExceptionCheck()) return nullptr;
        if (result == 2) return env->NewStringUTF("Cancelled. Completed shaders are saved; prepare again to resume.");
        // Per-shader failures are reported in the final summary and retried next time.
        return nullptr;
    } catch (const std::exception& e) {
        if (env->ExceptionCheck()) return nullptr;
        return env->NewStringUTF(e.what());
    }
}
#else
int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cerr << "Usage: compile_archive_shaders CACHE_DIR JOBS dataNNN.bin [dataNNN.bin ...]\n";
        return 2;
    }
    std::vector<fs::path> archives;
    for (int i = 3; i < argc; ++i) archives.emplace_back(argv[i]);
    try {
        return PrepareShaders(argv[1], std::clamp(std::atoi(argv[2]), 1, 16), archives,
            [](int, const std::string&) { return true; });
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
#endif
