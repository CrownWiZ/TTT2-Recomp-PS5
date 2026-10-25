#include "setup/mobile_install.h"
#include "setup/xdvdfs.h"
#include "setup/stfs.h"
#include "setup/game_data.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <memory>
#include <unistd.h>

namespace ttt2::setup {
namespace fs = std::filesystem;
namespace {
void Require(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }
void Write(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    out.close();
    Require(bool(out), "Could not write game data. Check free space.");
}
}
bool InstallFromDescriptors(int isoFd, int tuFd, const fs::path& destination,
                            const InstallProgress& progress, std::string& error) {
    // mkdtemp owns a new directory, even if an earlier attempt was interrupted.
    fs::path staging;
    try {
        auto report = [&](int pct, const std::string& msg) {
            Require(!progress || progress(pct, msg), "Installation cancelled.");
        };
        Require(!fs::exists(destination), "Game data already exists; it has not been changed.");
        report(0, "Checking disc image");
        XdvdfsImage iso;
        Require(iso.OpenFd(isoFd, error), error);
        auto* base = iso.Find("Default.xex");
        std::vector<uint8_t> xex, patch, update, index, image, nrfo;
        Require(base && iso.ReadEntry(*base, xex), "The ISO has no readable Default.xex.");
        Require(Sha1Hex(xex.data(), xex.size()) == kBaseXexSha1,
                "Use the supported USA/Europe retail Xbox 360 disc image.");
        report(1, "Checking Title Update 3");
        StfsPackage tu;
        Require(tu.OpenFd(tuFd, error), error);
        Require(tu.TitleId() == kTitleId && tu.MediaId() == kMediaId &&
                tu.ReadFile("Default.xexp", patch) && XexpTargetVersion(patch) == "0.0.3.2",
                "Select Title Update 3 for this disc (version 0.0.3.2, filename ending O2).");
        Require(tu.ReadFile("data100.bin", update) && tu.ReadFile("data100.ofs", index),
                "The title update is incomplete.");
        auto updateSize = RecordExpandedSize(index);
        Require(update.size() >= 16 && IsLzxContainer(update.data()) && updateSize == 7411280,
                "The title update data is damaged or unsupported.");
        // The patcher expects trusted XEX structures. Match the supported delta before parsing it.
        Require(Sha1Hex(patch.data(), patch.size()) == "d3d37636693c0ecf191133b1d322026a22a3250a",
                "The Title Update 3 executable patch is damaged or unsupported.");
        report(2, "Applying Title Update 3");
        Require(BuildGameImage(xex, patch, image, error), error);
        auto* nrfoEntry = iso.Find("nrfo.bin");
        Require(nrfoEntry && iso.ReadEntry(*nrfoEntry, nrfo), "The ISO has no readable nrfo.bin.");
        std::vector<std::string> names;
        for (const auto& e : iso.Entries())
            if (!e.dir && e.path.size() == 11 && e.path.substr(0,4) == "data" && e.path.substr(7) == ".bin")
                names.push_back(e.path);
        std::sort(names.begin(), names.end());
        Require(names.size() == 42 && std::adjacent_find(names.begin(), names.end()) == names.end(),
                "The ISO must contain 42 game archives.");
        auto sizes = ArchiveSizes(nrfo, names);
        struct Planned { const IsoEntry* entry; uint64_t size; bool expand; };
        std::vector<Planned> plan;
        uint64_t total = image.size() + updateSize + index.size();
        for (const auto& e : iso.Entries()) {
            if (e.dir || e.path.find('\\') != std::string::npos || CompareNames(e.path.c_str(), "AvatarAssetPack") == 0) continue;
            uint8_t head[4]{};
            Require(e.size < 4 || iso.ReadAt(e.offset, head, 4), "Cannot read the ISO.");
            bool expand = e.size >= 16 && IsLzxContainer(head);
            uint64_t size = expand ? sizes[e.path] : e.size;
            Require(size > 0 && size <= (2ull << 30), "An archive size is invalid.");
            plan.push_back({&e, size, expand}); total += size;
        }
        Require(plan.size() == 45 && total < (16ull << 30), "Unexpected disc contents.");
        // Disc directory order is not physical order. Avoid seeking around slow USB/NFS media.
        std::sort(plan.begin(), plan.end(), [](const Planned& a, const Planned& b) {
            return a.entry->offset < b.entry->offset;
        });
        fs::create_directories(destination.parent_path());
        Require(fs::space(destination.parent_path()).available >= total + (256ull << 20),
                "Not enough space. Free at least 13 GB in addition to the ISO and try again.");
        auto pattern = (destination.parent_path() / ".ttt2-install-XXXXXX").string();
        Require(mkdtemp(pattern.data()) != nullptr, "Cannot create an installation folder.");
        staging = pattern;
        fs::create_directories(staging / "game_files_x" / "update");
        uint64_t done = 0, shown = 0;
        auto bytes = [&](const std::string& msg, uint64_t n) {
            if (done + n >= shown + (8u << 20) || n == 0) {
                shown = done + n;
                report(3 + int((done + n) * 94 / total), msg);
            }
            return true;
        };
        std::vector<uint8_t> buffer(1u << 20);
        for (const auto& p : plan) {
            auto& e = *p.entry;
            fs::path out = staging / "game_files_x" / e.path;
            // Parser forbids path separators and traversal. Also refuse duplicate names.
            Require(!fs::exists(out), "The ISO contains duplicate filenames.");
            const std::string msg = "Extracting " + e.path;
            bytes(msg, 0);
            if (p.expand) {
                Require(ExpandContainer([&](uint64_t at, void* data, uint32_t n) {
                    return at <= e.size && n <= e.size - at && iso.ReadAt(e.offset + at, data, n);
                }, e.size, p.size, out, [&](uint64_t n) { return bytes(msg, n); }, error), error);
            } else {
                std::ofstream stream(out, std::ios::binary);
                for (uint64_t at = 0; at < e.size;) {
                    uint32_t n = uint32_t(std::min<uint64_t>(buffer.size(), e.size - at));
                    Require(iso.ReadAt(e.offset + at, buffer.data(), n), "Could not read the ISO.");
                    stream.write(reinterpret_cast<const char*>(buffer.data()), n);
                    Require(bool(stream), "Could not write game data. Check free space.");
                    at += n; bytes(msg, at);
                }
                stream.close(); Require(bool(stream), "Could not finish writing game data.");
            }
            done += p.size;
        }
        report(97, "Extracting title update data");
        Require(ExpandContainer([&](uint64_t at, void* data, uint32_t n) {
            if (at > update.size() || n > update.size() - at) return false;
            memcpy(data, update.data() + at, n); return true;
        }, update.size(), updateSize, staging / "game_files_x/update/data100.bin",
        [&](uint64_t) { report(97, "Extracting title update data"); return true; }, error), error);
        Write(staging / "game_files_x/update/data100.ofs", index);
        Write(staging / "tekken.img", image);
        // Size manifest permits quick completeness checks without hashing 13 GB on every launch.
        std::ofstream manifest(staging / "installed.txt");
        manifest << image.size() << " tekken.img\n";
        for (const auto& p : plan) manifest << p.size << " game_files_x/" << p.entry->path << '\n';
        manifest << updateSize << " game_files_x/update/data100.bin\n"
                 << index.size() << " game_files_x/update/data100.ofs\n";
        manifest.close(); Require(bool(manifest), "Could not write installation record.");
        report(99, "Finishing installation");
        // A single rename publishes all 48 files together. No game data is touched before this.
        Require(!fs::exists(destination), "Game data was installed by another setup.");
        fs::rename(staging, destination);
        staging.clear();
        if (progress) progress(100, "Game data is ready.");
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        if (!staging.empty()) { std::error_code ignored; fs::remove_all(staging, ignored); }
        return false;
    }
}
}
