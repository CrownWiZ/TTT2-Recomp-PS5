// Disc files remain read-only. Writes are restricted to a mounted save container
// provided by save_store; the optional store is isolated from extracted archives.
#include "kernel_shared.h"
#include "kernel_trace.h"
#include "kernel_objects.h"
#include "save_store.h"
#include "kernel_content.h"
#include "../host_vm.h"
#include "../guest_memory.h"
#include "../host_thread.h"
#include "../host_file.h"
#include <atomic>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <filesystem>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

static bool IoTrace()
{
    static const bool enabled = [] {
        const char* value = std::getenv("TTT2_IO_TRACE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

void RollbackBeforeHostWrite(uint32_t addr, uint32_t size);   // rollback.cpp
void RollbackNoteIo(bool frameOnly, const char* op = "", const PPCContext* ctx = nullptr);   // rollback.cpp: no rewind across I/O

// Where the extracted disc lives. TTT2_VFS_ROOT overrides it, which is how a tree of
// EXPANDED archives is selected (26 of the 42 dataNNN.bin are LZX containers on the disc and
// have to be expanded before the game can read them - see PROJECT_STATUS session 8).
static const std::filesystem::path& VfsRoot()
{
    static const std::filesystem::path root = [] {
        const char* path = std::getenv("TTT2_VFS_ROOT");
        return std::filesystem::path(path && *path ? path : "C:/ttt2/re/game_files");
    }();
    return root;
}
#define VFS_ROOT VfsRoot()

struct FileRecord
{
    host_file::File* handle;
    const std::string path;
    std::filesystem::path saveContainer;
    bool writable=false, directory=false;
    bool deleteAccess=false;          // guest DELETE / GENERIC_ALL: delete-disposition and rename
    std::string guestPath;            // as opened; the base for RootDirectory-relative opens
    bool directoryScanned=false;
    size_t directoryCursor=0;
    std::vector<host_file::DirectoryEntry> directoryItems;
    std::mutex mutex;
    FileRecord(host_file::File* h, std::string name) : handle(h), path(std::move(name)) {}
    ~FileRecord() { host_file::Close(handle); }
};

static std::mutex g_fmutex;
static std::unordered_map<uint32_t, std::shared_ptr<FileRecord>> g_files;
static uint32_t g_nextFile = 0xE0000000;   // file-handle range, below the kernel-object range (0xF...)
static std::vector<std::weak_ptr<FileRecord>> g_saveFiles;

uint32_t FlushSaveFiles(const std::filesystem::path& container,bool requireClosed)
{
    std::vector<std::shared_ptr<FileRecord>> files;
    { std::lock_guard lock(g_fmutex);
      for(auto it=g_saveFiles.begin();it!=g_saveFiles.end();) {
          if(auto f=it->lock()){if(f->saveContainer==container)files.push_back(f);++it;}
          else it=g_saveFiles.erase(it);
      }
    }
    if(requireClosed && !files.empty())return 170; // ERROR_BUSY
    for(auto& f:files){std::lock_guard lock(f->mutex);if(f->writable){uint32_t error=host_file::Flush(f->handle);if(error)return error;}}
    return 0;
}

static std::shared_ptr<FileRecord> GetFile(uint32_t handle)
{
    std::lock_guard l(g_fmutex);
    auto it = g_files.find(handle);
    return it == g_files.end() ? nullptr : it->second;
}

static constexpr uint32_t STATUS_END_OF_FILE = 0xC0000011u;
static constexpr uint32_t IO_STATUS_NAME_NOT_FOUND = 0xC0000034u;
static constexpr uint32_t IO_STATUS_INVALID_HANDLE        = 0xC0000008u;
static constexpr uint32_t IO_STATUS_INVALID_PARAMETER = 0xC000000Du;

static uint32_t FileErrorStatus(uint32_t error) { return host_file::DosErrorStatus(error); }

static std::string ReadObjAttrPath(uint8_t* base, uint32_t objAttrPtr)
{
    // XOBJECT_ATTRIBUTES: +0 RootDirectory, +4 ObjectName(ptr to ANSI_STRING), +8 Attributes.
    // XANSI_STRING: +0 Length(u16), +2 MaximumLength(u16), +4 Buffer(u32 guest ptr).
    if (!objAttrPtr) return {};
    uint32_t namePtr = GRead32(base, objAttrPtr + 4);
    if (!namePtr) return {};
    uint16_t len = GRead16(base, namePtr + 0);
    uint32_t buf = GRead32(base, namePtr + 4);
    std::string s;
    for (uint16_t i = 0; i < len && i < 1024; ++i) s += (char)GRead8(base, buf + i);
    // RootDirectory naming one of our file handles: the name is relative to it. TTT2 loads a
    // save by opening "tag25save:\\" as a directory and then "00_000_0.BIN" relative to it;
    // resolving that bare name against the disc made every save look damaged.
    const uint32_t root = GRead32(base, objAttrPtr + 0);
    if (root >= 0xE0000000u && root < 0xF0000000u && !s.empty())
        if (auto parent = GetFile(root); parent && parent->directory && !parent->guestPath.empty())
        {
            const std::string& p = parent->guestPath;
            const char last = p.back();
            s = p + (last == '\\' || last == '/' || last == ':' ? "" : "\\") + s;
        }
    return s;
}

static std::filesystem::path ResolvePath(std::string p)
{
    // Strip a drive ("game:\", "d:\") or device ("\Device\Cdrom0\") prefix, then look under VFS_ROOT.
    auto colon = p.find(':');
    // "update:" is the mounted title update, NOT the disc: only <VFS_ROOT>/update (TU3's
    // data100.bin - expanded - and data100.ofs). TTT2 probes update:\nrfo.bin first; serving the
    // disc's nrfo there made it index data100.bin with the disc table and never read
    // data100.ofs, so every TU3-overridden asset read garbage (the main menu never built).
    if (colon == 6 && std::equal(p.begin(), p.begin() + colon, "update",
        [](unsigned char a, unsigned char b) { return std::tolower(a) == b; }))
    {
        std::string rest = p.substr(colon + 1);
        for (auto& c : rest) if (c == '\\') c = '/';
        while (!rest.empty() && rest[0] == '/') rest.erase(0, 1);
        return host_file::ResolveDiscPath(VFS_ROOT,
            std::filesystem::path("update") / rest, false);
    }
    if (colon != std::string::npos)
        p = p.substr(colon + 1);
    else if (p.size() > 8 && (p.rfind("\\Device\\", 0) == 0))
    {
        int slashes = 0; size_t pos = 0;
        for (size_t i = 1; i < p.size(); ++i) if (p[i] == '\\' && ++slashes == 2) { pos = i + 1; break; }
        p = p.substr(pos);
    }
    for (auto& c : p) if (c == '\\') c = '/';
    while (!p.empty() && p[0] == '/') p.erase(0, 1);

    // The game also names some root data files through a disc subdirectory.
    return host_file::ResolveDiscPath(VFS_ROOT, p, true);
}

static std::string HostPathForLog(const std::filesystem::path& path)
{
    const auto utf8=path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()),utf8.size());
}

static void DoOpen(PPCContext& ctx, uint8_t* base,bool create)
{
    // NtCreateFile: r3=FileHandle*, r5=ObjectAttributes*, r6=IoStatusBlock*  (NtOpenFile matches these).
    uint32_t handleOut = ctx.r3.u32, objAttr = ctx.r5.u32, ioStatus = ctx.r6.u32;
    std::string guestPath = ReadObjAttrPath(base, objAttr);
    if (guestPath.empty() && objAttr && XamGuestRange(objAttr, 12))
    {
        // Diagnostics: the menu load issues a few opens whose name reads as empty.
        const uint32_t root = GRead32(base, objAttr), name = GRead32(base, objAttr + 4);
        std::fprintf(stderr, "[io] empty-name open: objattr=%08X root=%08X name=%08X attr=%08X len=%u max=%u buf=%08X access=%08X%s\n",
                     objAttr, root, name, GRead32(base, objAttr + 8),
                     name && XamGuestRange(name, 8) ? GRead16(base, name) : 0u,
                     name && XamGuestRange(name, 8) ? GRead16(base, name + 2) : 0u,
                     name && XamGuestRange(name, 8) ? GRead32(base, name + 4) : 0u, ctx.r4.u32,
                     create ? " (NtCreateFile)" : " (NtOpenFile)");
        // Guest backtrace: [r1] = caller frame, saved LR at [callerFrame - 8].
        char line[512];
        int n = std::snprintf(line, sizeof(line), "[io] empty-name open backtrace:");
        uint32_t frame = ctx.r1.u32;
        for (int i = 0; i < 16 && frame && frame < 0xFFFFFFF0u && n < 460; ++i)
        {
            const uint32_t prev = GRead32(base, frame);
            if (!prev || prev <= frame || prev - frame > 0x10000) break;
            n += std::snprintf(line + n, sizeof(line) - n, " %08X", GRead32(base, prev - 8));
            frame = prev;
        }
        std::snprintf(line + n, sizeof(line) - n, "\n");
        std::fputs(line, stderr);
    }
    std::filesystem::path savePath,container;uint32_t saveError=0;
    bool save=ResolveSaveFile(guestPath,savePath,container,saveError);
    std::filesystem::path host = save?savePath:ResolvePath(guestPath);
    const std::string hostLabel=HostPathForLog(host);
    host_file::File* h=nullptr;uint32_t status=STATUS_SUCCESS,information=1;
    const uint32_t writeMask=0x50010116u; // generic write/all, delete, write/append/EA/attributes
    const bool writable=(ctx.r4.u32&writeMask)!=0;
    bool directory=false;
    const char* stage="";
    if(saveError){status=FileErrorStatus(saveError);stage="resolve";}
    else if(!save) {
        if(writable || (create && ctx.r10.u32!=1))status=0xC0000022u;
        else {
            auto opened=host_file::OpenDisc(host);
            h=opened.file;status=opened.status;information=opened.information;stage=opened.stage;
        }
    } else {
        // NtCreateFile's ninth argument lives on the guest stack.
        if(create&&(!XamGuestRange(ctx.r1.u32,0x58)||(ctx.r7.u32&&!XamGuestRange(ctx.r7.u32,8)))) {
            ctx.r3.u32=IO_STATUS_INVALID_PARAMETER;return;
        }
        uint32_t options=create?GRead32(base,ctx.r1.u32+0x54):ctx.r8.u32;
        const uint32_t disposition=create?ctx.r10.u32:1;
        directory=(options&1)!=0;
        if(disposition>5 || (options&0x1000)){status=0xC0000002u;stage="options";}
        else {
            const bool hasAllocation=create&&ctx.r7.u32;
            const uint64_t allocation=hasAllocation ?
                (uint64_t(GRead32(base,ctx.r7.u32))<<32)|GRead32(base,ctx.r7.u32+4) : 0;
            auto opened=host_file::OpenSave(savePath,container,ctx.r4.u32,
                create?ctx.r9.u32:ctx.r7.u32,disposition,options,
                create?ctx.r8.u32:0x80u,hasAllocation,allocation);
            h=opened.file;status=opened.status;information=opened.information;stage=opened.stage;
        }
    }
    if (IoTrace())
    {
        char line[1200];
        if (save)   // save paths: the reason matters (mount state, access, disposition, options)
            std::snprintf(line, sizeof(line), "[io] open '%s' -> %s status=%08X stage=%s access=%08X disp=%u opts=%08X%s\n",
                          guestPath.c_str(), h == nullptr ? "FAILED" : hostLabel.c_str(), status, stage,
                          ctx.r4.u32, create ? ctx.r10.u32 : 1u,
                          create ? (XamGuestRange(ctx.r1.u32, 0x58) ? GRead32(base, ctx.r1.u32 + 0x54) : 0u) : ctx.r8.u32,
                          saveError ? " (not mounted / invalid)" : "");
        else
            std::snprintf(line, sizeof(line), "[io] open '%s' -> %s\n", guestPath.c_str(),
                          h == nullptr ? "NOT FOUND" : hostLabel.c_str());
        std::fputs(line, stderr);
    }
    if (status&0x80000000u)
    {
        if (ioStatus) { GWrite32(base, ioStatus + 0, status); GWrite32(base, ioStatus + 4, 0); }
        ctx.r3.u32 = status;
        return;
    }
    uint32_t handle;
    {
        std::lock_guard l(g_fmutex);
        handle = g_nextFile++;
        g_files[handle] = std::make_shared<FileRecord>(h,
            HostPathForLog(host.filename()));
        auto& file=g_files[handle];file->writable=save&&(ctx.r4.u32&0x50000006u);file->directory=directory;
        file->guestPath=guestPath;
        file->deleteAccess=save&&(ctx.r4.u32&0x10010000u);
        if(save){file->saveContainer=container;g_saveFiles.push_back(file);}
    }
    GWrite32(base, handleOut, handle);
    if (ioStatus) { GWrite32(base, ioStatus + 0, STATUS_SUCCESS); GWrite32(base, ioStatus + 4, information); }
    ctx.r3.u32 = STATUS_SUCCESS;
}

// Called from NtClose (kernel_objects.cpp) for handles in the file range.
bool TryCloseGuestFile(uint32_t handle)
{
    if (handle < 0xE0000000 || handle >= 0xF0000000) return false;
    std::lock_guard l(g_fmutex);
    auto it = g_files.find(handle);
    // An operation that already looked up this file retains the host handle until it
    // completes. Otherwise closing the host file could race a read or handle reuse.
    if (it != g_files.end()) g_files.erase(it);
    return true;
}

// TTT2_FRAMETIME=1: host file calls over 5 ms (the caller waits for them: reads are synchronous).
// Per-thread totals of the host file calls (TTT2_FRAMETIME): diag_hooks.cpp reads them around slow tasks.
struct ThreadIo { uint64_t calls = 0, bytes = 0; double ms = 0; };
thread_local ThreadIo g_threadIo;
void ThreadIoTotals(uint64_t& calls, uint64_t& bytes, double& ms) { calls = g_threadIo.calls; bytes = g_threadIo.bytes; ms = g_threadIo.ms; }
static void LogSlowIo(const char* what, std::chrono::steady_clock::time_point start, uint32_t length)
{
    static const bool on = [] { const char* e = std::getenv("TTT2_FRAMETIME"); return e && *e == '1'; }();
    if (!on) return;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    g_threadIo.calls += 1;
    g_threadIo.bytes += length;
    g_threadIo.ms += ms;
    static std::atomic<int> logged{ 0 };
    if (ms > 5.0 && logged++ < 300) std::fprintf(stderr, "[frame] slow %s %.1f ms (%u bytes) thread %u\n", what, ms, length, host_thread::CurrentId());
}
PPC_FUNC(__imp__NtCreateFile) { KTrace("NtCreateFile"); RollbackNoteIo(true, "NtCreateFile", &ctx); const auto t = std::chrono::steady_clock::now(); DoOpen(ctx, base,true); LogSlowIo("open", t, 0); }
PPC_FUNC(__imp__NtOpenFile)   { KTrace("NtOpenFile");   RollbackNoteIo(true, "NtOpenFile", &ctx); const auto t = std::chrono::steady_clock::now(); DoOpen(ctx, base,false); LogSlowIo("open", t, 0); }

PPC_FUNC(__imp__NtReadFile)
{
    KTrace("NtReadFile");
    RollbackNoteIo(true, "NtReadFile", &ctx);
    // r3=FileHandle, r4=Event, r5=ApcRoutine, r6=ApcContext,
    // r7=IoStatusBlock, r8=Buffer, r9=Length, r10=ByteOffset* (LARGE_INTEGER, optional)
    auto file = GetFile(ctx.r3.u32);
    uint32_t event = ctx.r4.u32, apcRoutine = ctx.r5.u32, apcContext = ctx.r6.u32;
    uint32_t ioStatus = ctx.r7.u32, buffer = ctx.r8.u32, length = ctx.r9.u32, byteOffPtr = ctx.r10.u32;
    if (!file) { ctx.r3.u32 = IO_STATUS_INVALID_HANDLE; return; }
    int64_t effOff = -1;
    if (byteOffPtr)
    {
        uint64_t off = ((uint64_t)GRead32(base, byteOffPtr) << 32) | GRead32(base, byteOffPtr + 4);
        effOff = (int64_t)off;
        // FILE_USE_FILE_POINTER_POSITION (-2) and a null offset both use the current
        // position. Other negative offsets fail before an I/O is submitted.
        if (effOff < 0 && effOff != -2) { ctx.r3.u32 = IO_STATUS_INVALID_PARAMETER; return; }
    }
    uint32_t read = 0;
    uint32_t status = STATUS_SUCCESS;
    const auto ioStart = std::chrono::steady_clock::now();
    {
        // The host handle is synchronous. Its shared file pointer needs one lock across
        // the entire seek/read sequence, including NtSet/QueryInformationFile calls.
        std::lock_guard fileLock(file->mutex);
        if (length != 0)
        {
            uint64_t original = 0, position = 0;
            status = host_file::Seek(file->handle, 0, host_file::Origin::Current, original);
            if (status == STATUS_SUCCESS)
            {
                const bool explicitOffset = byteOffPtr && effOff != -2;
                if (explicitOffset)
                    status = host_file::Seek(file->handle, effOff,
                                             host_file::Origin::Begin, position);
                else effOff = int64_t(original);
                if (status == STATUS_SUCCESS)
                {
                    RollbackBeforeHostWrite(buffer, length);
                    if (!host_vm::PrepareHostWrite(base + buffer, length))
                        status = STATUS_UNSUCCESSFUL;
                    else status = host_file::Read(file->handle, base + buffer, length, read);
                    if (status == STATUS_SUCCESS && read == 0) status = STATUS_END_OF_FILE;
                    // NT preserves the old file position on EOF or failure for
                    // explicit-offset reads. Partial reads still succeed.
                    if (explicitOffset && (read == 0 || status != STATUS_SUCCESS))
                    {
                        const uint32_t restore = host_file::Seek(file->handle,
                            int64_t(original), host_file::Origin::Begin, position);
                        if (restore) status = restore;
                    }
                }
            }
        }
        // A valid zero-length read succeeds without changing the file position.
    }
    LogSlowIo("read", ioStart, length);
    if (IoTrace())
    {
        static std::atomic<uint64_t> dlog{0};
        uint64_t dn = ++dlog;
        if (dn <= 300 || dn % 200 == 0)
            std::fprintf(stderr, "[io] read h=%08X '%s' off=%lld len=%u got=%lu status=%08X\n",
                         ctx.r3.u32, file->path.c_str(), (long long)effOff, length, (unsigned long)read, status);
    }
    if (IoTrace() && status == STATUS_END_OF_FILE)
    {
        static std::atomic<uint32_t> eoflog{0};
        if (++eoflog <= 40)
            std::fprintf(stderr, "[io] EOF read '%s' off=%lld len=%u\n",
                         file->path.c_str(), (long long)effOff, length);
    }
    if (ioStatus) { GWrite32(base, ioStatus + 0, status); GWrite32(base, ioStatus + 4, read); }
    // The NT kernel signals the caller's event when an I/O completes (even synchronously);
    // async loaders wait on it after issuing the read.
    if (event) KernelSignalEventHandle(event);
    // Deliver the I/O completion APC: ApcRoutine(ApcContext, IoStatusBlock, 0) - on the
    // dispatcher thread (inline delivery would reuse the caller's in-use guest stack).
    if (apcRoutine)
    {
        static std::atomic<unsigned> alog{0};
        if (++alog <= 8)
        {
            std::fprintf(stderr, "[io] read APC -> %08X(ctx=%08X, iosb=%08X)\n",
                         apcRoutine, apcContext, ioStatus);
        }
        KernelQueueGuestCall(apcRoutine, apcContext, ioStatus, 0, 0);
    }
    static std::atomic<uint64_t> reads{0}, bytes{0};
    bytes += read;
    uint64_t n = ++reads;
    if (n <= 5 || n % 200 == 0)
    {
        char line[160];
        std::snprintf(line, sizeof(line), "[io] read #%llu h=%08X len=%u got=%u total=%lluKB\n",
                      (unsigned long long)n, ctx.r3.u32, length, read, (unsigned long long)(bytes / 1024));
        std::fputs(line, stderr);
    }
    ctx.r3.u32 = status;
}

PPC_FUNC(__imp__NtSetInformationFile)
{
    KTrace("NtSetInformationFile");
    RollbackNoteIo(true, "NtSetInformationFile", &ctx);     // seeks by streaming threads are theirs; deletes come from the frame
    // r3=handle, r4=IoStatusBlock, r5=FileInformation, r6=Length, r7=FileInformationClass
    auto file = GetFile(ctx.r3.u32);
    if (!file) { ctx.r3.u32 = IO_STATUS_INVALID_HANDLE; return; }
    uint32_t status = STATUS_SUCCESS;
    if (ctx.r7.u32 == 14 /*FilePositionInformation*/)
    {
        if (!ctx.r5.u32 || ctx.r6.u32 < 8) { ctx.r3.u32 = IO_STATUS_INVALID_PARAMETER; return; }
        uint64_t pos = ((uint64_t)GRead32(base, ctx.r5.u32) << 32) | GRead32(base, ctx.r5.u32 + 4);
        if ((int64_t)pos < 0) { ctx.r3.u32 = IO_STATUS_INVALID_PARAMETER; return; }
        std::lock_guard fileLock(file->mutex);
        uint64_t position=0;
        status=host_file::Seek(file->handle,int64_t(pos),host_file::Origin::Begin,position);
    }
    else if(ctx.r7.u32==20 && file->writable && !file->saveContainer.empty()) {
        if(!XamGuestRange(ctx.r5.u32,8)||ctx.r6.u32<8){ctx.r3.u32=IO_STATUS_INVALID_PARAMETER;return;}
        const uint64_t size=(uint64_t(GRead32(base,ctx.r5.u32))<<32)|GRead32(base,ctx.r5.u32+4);
        if(int64_t(size)<0){ctx.r3.u32=IO_STATUS_INVALID_PARAMETER;return;}
        std::lock_guard fileLock(file->mutex);
        status=host_file::SetSize(file->handle,size);
    }
    else if(ctx.r7.u32==13 && !file->saveContainer.empty()) {
        // FileDispositionInformation { BOOLEAN DeleteFile }: removed when the last handle closes.
        if(!XamGuestRange(ctx.r5.u32,1)||ctx.r6.u32<1){ctx.r3.u32=IO_STATUS_INVALID_PARAMETER;return;}
        if(!file->deleteAccess||file->directory)status=0xC0000022u;
        else {
            const bool remove=GRead8(base,ctx.r5.u32)!=0;
            std::lock_guard fileLock(file->mutex);
            status=host_file::SetDeleteDisposition(file->handle,remove);
        }
        if (IoTrace()) std::fprintf(stderr,"[io] delete-disposition '%s' -> %08X\n",file->path.c_str(),status);
    }
    else if(ctx.r7.u32==10 && !file->saveContainer.empty()) {
        // FileRenameInformation (Xenia X_FILE_RENAME_INFORMATION): +0 ReplaceIfExists, +4 root
        // directory handle, +8 ANSI_STRING {length, maximum, buffer}. The target must be a flat
        // name in the SAME save container (TTT2 writes NN_000_0.BIN, then renames it to _1).
        if(!XamGuestRange(ctx.r5.u32,16)||ctx.r6.u32<16){ctx.r3.u32=IO_STATUS_INVALID_PARAMETER;return;}
        const bool replace=GRead32(base,ctx.r5.u32)!=0;
        const uint32_t root=GRead32(base,ctx.r5.u32+4),length=GRead16(base,ctx.r5.u32+8),buffer=GRead32(base,ctx.r5.u32+12);
        std::string target;
        if(length&&XamGuestRange(buffer,length,1))for(uint32_t i=0;i<length;++i)target+=char(GRead8(base,buffer+i));
        std::filesystem::path host,container;uint32_t error=0;
        if(root)status=0xC00000BBu;                                         // STATUS_NOT_SUPPORTED
        else if(!file->deleteAccess||file->directory)status=0xC0000022u;
        else if(target.empty()||!ResolveSaveFile(target,host,container,error)||error||
                container!=file->saveContainer||host==container)status=error?FileErrorStatus(error):0xC0000033u;
        else {
            std::lock_guard fileLock(file->mutex);
            status=host_file::Rename(file->handle,host,replace);
        }
        if (IoTrace()) std::fprintf(stderr,"[io] rename '%s' -> '%s' replace=%d -> %08X\n",file->path.c_str(),target.c_str(),replace?1:0,status);
    }
    else if(!file->saveContainer.empty())status=0xC0000002u;
    if (ctx.r4.u32) { GWrite32(base, ctx.r4.u32 + 0, status); GWrite32(base, ctx.r4.u32 + 4, 0); }
    ctx.r3.u32 = status;
}

PPC_FUNC(__imp__NtQueryInformationFile)
{
    KTrace("NtQueryInformationFile");
    // r3=handle, r4=IoStatusBlock, r5=FileInformation OUT, r6=Length, r7=FileInformationClass
    auto file = GetFile(ctx.r3.u32);
    if (!file) { ctx.r3.u32 = IO_STATUS_INVALID_HANDLE; return; }
    uint32_t out = ctx.r5.u32, cls = ctx.r7.u32;
    uint32_t status = STATUS_SUCCESS;
    bool handled = false;
    uint64_t size=0;
    std::lock_guard fileLock(file->mutex);
    if (!file->directory) status=host_file::Size(file->handle,size);
    if (status == STATUS_SUCCESS && out)
    {
        switch (cls)
        {
        case 4:  // FileBasicInformation: 4 timestamps(8) + FileAttributes(4)
            for (int i = 0; i < 0x20; i += 4) GWrite32(base, out + i, 0);
            GWrite32(base, out + 0x20, file->directory?0x10u:0x80u);
            handled = true;
            break;
        case 5:  // FileStandardInformation: AllocationSize @0, EndOfFile @8, links/delete/dir
            GWrite32(base, out + 0, (uint32_t)(size >> 32)); GWrite32(base, out + 4, (uint32_t)size);
            GWrite32(base, out + 8, (uint32_t)(size >> 32)); GWrite32(base, out + 12, (uint32_t)size);
            GWrite32(base, out + 16, 1); GWrite32(base, out + 20, 0);
            if(file->directory)GWrite8(base,out+21,1);
            handled = true;
            break;
        case 6:  // FileInternalInformation: 8-byte file index
            GWrite32(base, out + 0, 0); GWrite32(base, out + 4, ctx.r3.u32);
            handled = true;
            break;
        case 7:  // FileEaInformation: EaSize
            GWrite32(base, out + 0, 0);
            handled = true;
            break;
        case 27: // FileModeInformation: no FILE_SYNCHRONOUS_IO_* flags (handle opened async)
            GWrite32(base, out + 0, 0);
            handled = true;
            break;
        case 14: // FilePositionInformation
        {
            if (ctx.r6.u32 < 8) { status = IO_STATUS_INVALID_PARAMETER; break; }
            uint64_t current=0;
            status=host_file::Seek(file->handle,0,host_file::Origin::Current,current);
            if(status)break;
            GWrite32(base, out + 0, (uint32_t)(current >> 32)); GWrite32(base, out + 4, (uint32_t)current);
            handled = true;
            break;
        }
        case 34: // FileNetworkOpenInformation: times @0..0x18, AllocationSize @0x20, EndOfFile @0x28, Attributes @0x30
            for (int i = 0; i < 0x20; i += 4) GWrite32(base, out + i, 0);
            GWrite32(base, out + 0x20, (uint32_t)(size >> 32)); GWrite32(base, out + 0x24, (uint32_t)size);
            GWrite32(base, out + 0x28, (uint32_t)(size >> 32)); GWrite32(base, out + 0x2C, (uint32_t)size);
            GWrite32(base, out + 0x30, file->directory?0x10u:0x80u); GWrite32(base, out + 0x34, 0);
            handled = true;
            break;
        }
    }
    if (IoTrace())
    {
        static std::atomic<uint32_t> qlog{0};
        if (++qlog <= 200 || !handled)
            std::fprintf(stderr, "[io] qinfo h=%08X '%s' class=%u len=%u size=%lld%s\n",
                         ctx.r3.u32, file->path.c_str(), cls, ctx.r6.u32,
                         (long long)size, handled ? "" : " UNHANDLED");
    }
    if (ctx.r4.u32) { GWrite32(base, ctx.r4.u32 + 0, status); GWrite32(base, ctx.r4.u32 + 4, status == STATUS_SUCCESS ? ctx.r6.u32 : 0); }
    ctx.r3.u32 = status;
}

// Query attributes (used to test existence / get size) by path in the object attributes.
PPC_FUNC(__imp__NtQueryFullAttributesFile)
{
    KTrace("NtQueryFullAttributesFile");
    // r3=ObjectAttributes*, r4=FILE_NETWORK_OPEN_INFORMATION*
    std::string guestPath = ReadObjAttrPath(base, ctx.r3.u32);
    std::filesystem::path savePath,container;uint32_t saveError=0;
    bool save=ResolveSaveFile(guestPath,savePath,container,saveError);
    if(saveError){ctx.r3.u32=FileErrorStatus(saveError);return;}
    std::filesystem::path host = save?savePath:ResolvePath(guestPath);
    const std::string hostLabel=HostPathForLog(host);
    uint64_t sz=0;bool directory=false;
    const uint32_t queryStatus=host_file::QueryAttributes(host,
        save?container:std::filesystem::path{},sz,directory);
    {
        char line[1200];
        std::snprintf(line,sizeof(line),"[io] query '%s' -> %s status=%08X\n",
                      guestPath.c_str(),queryStatus?"FAILED":hostLabel.c_str(),queryStatus);
        std::fputs(line,stderr);
    }
    if(!queryStatus)
    {
        uint32_t o = ctx.r4.u32;
        // FILE_NETWORK_OPEN_INFORMATION: times @0..0x18, AllocationSize @0x20, EndOfFile @0x28, Attributes @0x30
        if (o) { for (int i = 0; i < 0x38; i += 4) GWrite32(base, o + i, 0);
                 GWrite32(base, o + 0x20, (uint32_t)(sz >> 32)); GWrite32(base, o + 0x24, (uint32_t)sz);
                 GWrite32(base, o + 0x28, (uint32_t)(sz >> 32)); GWrite32(base, o + 0x2C, (uint32_t)sz);
                 GWrite32(base, o + 0x30, directory?0x10u:0x80u); }
        ctx.r3.u32 = STATUS_SUCCESS;
    }
    else ctx.r3.u32 = queryStatus;
}

PPC_FUNC(__imp__NtWriteFile)
{
    KTrace("NtWriteFile");RollbackNoteIo(false, "NtWriteFile", &ctx);auto file=GetFile(ctx.r3.u32);
    if(!file){ctx.r3.u32=IO_STATUS_INVALID_HANDLE;return;}
    if(ctx.r5.u32){ctx.r3.u32=0xC0000002u;return;} // initiating-thread APC delivery unavailable
    const uint32_t ios=ctx.r7.u32,buffer=ctx.r8.u32,length=ctx.r9.u32,offsetPtr=ctx.r10.u32;
    if(!XamGuestRange(ios,8)||(length&&!XamGuestRange(buffer,length,1))||(offsetPtr&&!XamGuestRange(offsetPtr,8))) {ctx.r3.u32=IO_STATUS_INVALID_PARAMETER;return;}
    int64_t offset=-2;if(offsetPtr)offset=int64_t((uint64_t(GRead32(base,offsetPtr))<<32)|GRead32(base,offsetPtr+4));
    if(offset<0 && offset!=-1 && offset!=-2){ctx.r3.u32=IO_STATUS_INVALID_PARAMETER;return;}
    uint32_t written=0;uint32_t status=0;
    {std::lock_guard fileLock(file->mutex);
     if(!file->writable || file->saveContainer.empty() || file->directory)status=0xC0000022u;
     else if(length) {
        uint64_t original=0,position=0;
        status=host_file::Seek(file->handle,0,host_file::Origin::Current,original);
        if(!status) {
            if(offset!=-2)
                status=host_file::Seek(file->handle,offset==-1?0:offset,
                    offset==-1?host_file::Origin::End:host_file::Origin::Begin,position);
            if(!status)status=host_file::Write(file->handle,base+buffer,length,written);
            if(status && offset!=-2)host_file::Seek(file->handle,int64_t(original),host_file::Origin::Begin,position);
        }
     }}
    GWrite32(base,ios,status);GWrite32(base,ios+4,written);
    if(ctx.r4.u32)KernelSignalEventHandle(ctx.r4.u32);ctx.r3.u32=status;
}
PPC_FUNC(__imp__NtWriteFileGather) { KTrace("NtWriteFileGather");ctx.r3.u32=0xC0000002u; }
PPC_FUNC(__imp__NtFlushBuffersFile)
{
    KTrace("NtFlushBuffersFile");auto file=GetFile(ctx.r3.u32);
    if(!file){ctx.r3.u32=IO_STATUS_INVALID_HANDLE;return;}
    uint32_t status=0;{std::lock_guard fileLock(file->mutex);if(file->writable)status=FileErrorStatus(host_file::Flush(file->handle));}
    if(ctx.r4.u32){GWrite32(base,ctx.r4.u32,status);GWrite32(base,ctx.r4.u32+4,0);}ctx.r3.u32=status;
}
PPC_FUNC(__imp__NtQueryDirectoryFile)
{
    KTrace("NtQueryDirectoryFile");auto file=GetFile(ctx.r3.u32);
    if(!file){ctx.r3.u32=IO_STATUS_INVALID_HANDLE;return;}
    const auto ios=ctx.r7.u32,out=ctx.r8.u32,bytes=ctx.r9.u32,patternPtr=ctx.r10.u32;
    if(ctx.r5.u32 || file->saveContainer.empty()){ctx.r3.u32=0xC0000002u;return;}
    if(!file->directory){ctx.r3.u32=0xC0000103u;return;}
    if(!XamGuestRange(ios,8)||!XamGuestRange(out,bytes,1)||!XamGuestRange(ctx.r1.u32,0x58)){ctx.r3.u32=IO_STATUS_INVALID_PARAMETER;return;}
    std::string pattern="*";
    if(patternPtr){
        if(!XamGuestRange(patternPtr,8)){ctx.r3.u32=IO_STATUS_INVALID_PARAMETER;return;}
        uint32_t len=GRead16(base,patternPtr),p=GRead32(base,patternPtr+4);
        if(len>42 || (len&&!XamGuestRange(p,len,1))){ctx.r3.u32=IO_STATUS_INVALID_PARAMETER;return;}
        pattern.assign(reinterpret_cast<const char*>(base+p),len);
    }
    const bool wildcard=pattern.empty()||pattern=="*"||pattern=="*.*";
    if(!wildcard&&pattern.find_first_of("*?\\/:")!=std::string::npos){ctx.r3.u32=0xC0000002u;return;}
    uint32_t status=0,returned=0;
    {std::lock_guard fileLock(file->mutex);
     if(GRead32(base,ctx.r1.u32+0x54)){file->directoryScanned=false;file->directoryCursor=0;file->directoryItems.clear();}
     if(!file->directoryScanned){
        std::vector<host_file::DirectoryEntry> listed;
        status=host_file::ListRegularFiles(file->handle,listed);
        file->directoryItems.clear();
        if(!status)for(const auto& item:listed){
            const auto& name=item.name;
            if(wildcard || (name.size()==pattern.size() &&
                std::equal(name.begin(),name.end(),pattern.begin(),
                    [](unsigned char a,unsigned char b){return std::tolower(a)==std::tolower(b); })))
                file->directoryItems.push_back(item);
        }
        if(!status){std::sort(file->directoryItems.begin(),file->directoryItems.end(),[](auto& a,auto& b){return a.name<b.name;});file->directoryScanned=true;}
     }
     if(!status){
        if(file->directoryCursor==file->directoryItems.size())status=0x80000006u;
        else {
            const auto& item=file->directoryItems[file->directoryCursor];uint32_t required=64+uint32_t(item.name.size());
            if(bytes<required)status=0xC0000023u;
            else {
                guest_memory::FillGuest(base, base+out,0,required);GWrite32(base,out+4,uint32_t(file->directoryCursor));
                GWrite32(base,out+40,uint32_t(item.size>>32));GWrite32(base,out+44,uint32_t(item.size));
                uint64_t allocated=(item.size+4095)&~uint64_t(4095);GWrite32(base,out+48,uint32_t(allocated>>32));GWrite32(base,out+52,uint32_t(allocated));
                GWrite32(base,out+56,0x80u);GWrite32(base,out+60,uint32_t(item.name.size()));
                guest_memory::CopyToGuest(base, base+out+64,item.name.data(),item.name.size());returned=required;++file->directoryCursor;
            }
        }
     }}
    GWrite32(base,ios,status);GWrite32(base,ios+4,returned);if(ctx.r4.u32)KernelSignalEventHandle(ctx.r4.u32);ctx.r3.u32=status;
}
PPC_FUNC(__imp__NtQueryVolumeInformationFile)
{
    KTrace("NtQueryVolumeInformationFile");
    // r3=handle, r4=IoStatusBlock, r5=FsInformation OUT, r6=Length, r7=FsInformationClass
    uint32_t out = ctx.r5.u32, cls = ctx.r7.u32;
    bool handled = false;
    if (out)
    {
        switch (cls)
        {
        case 3: // FileFsSizeInformation: TotalAllocationUnits(8), AvailableAllocationUnits(8), SectorsPerAllocationUnit(4), BytesPerSector(4)
            GWrite32(base, out + 0, 0); GWrite32(base, out + 4, 0x00400000);
            GWrite32(base, out + 8, 0); GWrite32(base, out + 12, 0);
            GWrite32(base, out + 16, 1); GWrite32(base, out + 20, 2048);
            handled = true;
            break;
        case 4: // FileFsDeviceInformation: DeviceType(4), Characteristics(4)
            GWrite32(base, out + 0, 0x2 /*FILE_DEVICE_CD_ROM*/); GWrite32(base, out + 4, 0x1 /*REMOVABLE_MEDIA*/);
            handled = true;
            break;
        }
    }
    std::fprintf(stderr, "[io] qvol h=%08X class=%u len=%u%s\n", ctx.r3.u32, cls, ctx.r6.u32,
                 handled ? "" : " UNHANDLED");
    if (ctx.r4.u32) { GWrite32(base, ctx.r4.u32, STATUS_SUCCESS); GWrite32(base, ctx.r4.u32 + 4, ctx.r6.u32); }
    ctx.r3.u32 = STATUS_SUCCESS;
}
