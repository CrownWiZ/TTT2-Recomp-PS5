#include <d3dcompiler.h>
#include <vkd3d_shader.h>

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

namespace {
class Blob final : public ID3DBlob {
public:
    Blob(const void* bytes, size_t size)
        : bytes_(static_cast<const char*>(bytes), static_cast<const char*>(bytes) + size) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        const GUID unknown = __uuidof(IUnknown);
        const GUID blob = __uuidof(ID3DBlob);
        if (std::memcmp(&iid, &unknown, sizeof(GUID)) &&
            std::memcmp(&iid, &blob, sizeof(GUID))) return E_NOINTERFACE;
        *object = static_cast<ID3DBlob*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining = --refs_;
        if (!remaining) delete this;
        return remaining;
    }
    void* STDMETHODCALLTYPE GetBufferPointer() override { return bytes_.data(); }
    SIZE_T STDMETHODCALLTYPE GetBufferSize() override { return bytes_.size(); }

private:
    std::atomic<ULONG> refs_{1};
    std::vector<char> bytes_;
};
}

extern "C" HRESULT WINAPI D3DCompile(const void* data, SIZE_T data_size, const char* filename,
    const D3D_SHADER_MACRO* defines, ID3DInclude* include, const char* entrypoint,
    const char* target, UINT, UINT, ID3DBlob** shader, ID3DBlob** error_messages)
{
    if (shader) *shader = nullptr;
    if (error_messages) *error_messages = nullptr;
    if (!data || !shader || !target || defines || include) return E_INVALIDARG;

    const vkd3d_shader_hlsl_source_info hlsl{
        VKD3D_SHADER_STRUCTURE_TYPE_HLSL_SOURCE_INFO, nullptr, entrypoint, {}, target};
    vkd3d_shader_compile_info info{};
    info.type = VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO;
    info.next = &hlsl;
    info.source = {data, data_size};
    info.source_type = VKD3D_SHADER_SOURCE_HLSL;
    info.target_type = VKD3D_SHADER_TARGET_DXBC_TPF;
    info.log_level = VKD3D_SHADER_LOG_WARNING;
    info.source_name = filename;

    vkd3d_shader_code compiled{};
    char* messages = nullptr;
    const int result = vkd3d_shader_compile(&info, &compiled, &messages);
    if (error_messages && messages) *error_messages = new Blob(messages, std::strlen(messages));
    if (messages) vkd3d_shader_free_messages(messages);
    if (!result) *shader = new Blob(compiled.code, compiled.size);
    if (compiled.code) vkd3d_shader_free_shader_code(&compiled);
    return result ? E_FAIL : S_OK;
}
