// Explicit ConPTY API-family ownership; original implementation, MIT.
#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace tf_conpty {
class Backend {
public:
    enum class GlyphWidth { Default, Console };
private:
    struct Api {
        HMODULE module = nullptr;
        decltype(&::CreatePseudoConsole) create = ::CreatePseudoConsole;
        decltype(&::ResizePseudoConsole) resize = ::ResizePseudoConsole;
        decltype(&::ClosePseudoConsole) close = ::ClosePseudoConsole;
        ~Api() { if (module) FreeLibrary(module); }
    };
    std::shared_ptr<const Api> api_;
    DWORD creation_flags_ = 0;
    explicit Backend(std::shared_ptr<const Api> api, DWORD flags = 0)
        : api_(std::move(api)), creation_flags_(flags) {}

    // Keep both files open without write/delete sharing from verification
    // through module load, so the checked pair cannot be replaced mid-load.
    class File {
        HANDLE handle_ = INVALID_HANDLE_VALUE;
    public:
        explicit File(const std::filesystem::path& path) {
            handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("ConPTY backend file unavailable");
        }
        ~File() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
        File(const File&) = delete;
        File& operator=(const File&) = delete;
        HANDLE get() const { return handle_; }
    };
    static std::string digest(HANDLE file) {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        struct Cleanup {
            BCRYPT_ALG_HANDLE& algorithm;
            BCRYPT_HASH_HANDLE& hash;
            ~Cleanup() {
                if (hash) BCryptDestroyHash(hash);
                if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
            }
        } cleanup{algorithm, hash};
        const auto check = [](NTSTATUS status) {
            if (status < 0) throw std::runtime_error("ConPTY backend identity check failed");
        };
        check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
        check(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0));
        std::array<unsigned char, 65536> buffer{};
        DWORD count = 0;
        size_t total = 0;
        for (;;) {
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
                throw std::runtime_error("ConPTY backend identity read failed");
            if (!count) break;
            total += count;
            if (total > 8 * 1024 * 1024) throw std::runtime_error("ConPTY backend file too large");
            check(BCryptHashData(hash, buffer.data(), count, 0));
        }
        std::array<unsigned char, 32> bytes{};
        check(BCryptFinishHash(hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0));
        static constexpr char hex[] = "0123456789abcdef";
        std::string value;
        for (const auto byte : bytes) { value += hex[byte >> 4]; value += hex[byte & 15]; }
        return value;
    }
public:
    static Backend system() {
        static const auto api = std::make_shared<const Api>();
        return Backend(api);
    }
    static Backend reviewed_side_by_side(const std::filesystem::path& directory,
                                        GlyphWidth width = GlyphWidth::Default) {
        if (sizeof(void*) != 8 || !directory.is_absolute())
            throw std::runtime_error("ConPTY reviewed backend requires x64 and an absolute directory");
        const auto canonical = std::filesystem::canonical(directory);
        const auto dll = canonical / L"conpty.dll";
        File dll_file(dll), host_file(canonical / L"OpenConsole.exe");
        if (digest(dll_file.get()) != "feeef341d891643c62d30b6b07800bc70f0bc148f44cb8c3bee84aa557ae805a" ||
            digest(host_file.get()) != "3d66b23d0a71bb8eed2b77edc8b9df9bf54ce6c8fb74c863a30e760997f80586")
            throw std::runtime_error("ConPTY reviewed backend identity mismatch");
        auto api = std::make_shared<Api>();
        api->module = LoadLibraryExW(dll.c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!api->module) throw std::runtime_error("ConPTY reviewed backend load failed");
        api->create = reinterpret_cast<decltype(api->create)>(GetProcAddress(api->module, "ConptyCreatePseudoConsole"));
        api->resize = reinterpret_cast<decltype(api->resize)>(GetProcAddress(api->module, "ConptyResizePseudoConsole"));
        api->close = reinterpret_cast<decltype(api->close)>(GetProcAddress(api->module, "ConptyClosePseudoConsole"));
        if (!api->create || !api->resize || !api->close)
            throw std::runtime_error("ConPTY reviewed backend API missing");
        wchar_t loaded[32768]{};
        const auto count = GetModuleFileNameW(api->module, loaded, 32768);
        if (!count || count >= 32768 || !std::filesystem::equivalent(dll, loaded))
            throw std::runtime_error("ConPTY reviewed backend module identity differs");
        if (width != GlyphWidth::Default && width != GlyphWidth::Console)
            throw std::runtime_error("ConPTY reviewed glyph width policy invalid");
        // Fixed official 1.25 conpty.h: GLYPH_WIDTH_CONSOLE=0x18. This is
        // a console API compatibility policy, not the removed passthrough.
        return Backend(std::move(api), width == GlyphWidth::Console ? 0x18 : 0);
    }
    static Backend configured() {
#ifdef TERMFORGE_CONPTY_SIDEBYSIDE
        wchar_t image[32768]{};
        const auto count = GetModuleFileNameW(nullptr, image, 32768);
        if (!count || count >= 32768) throw std::runtime_error("ConPTY backend executable location unavailable");
        return reviewed_side_by_side(std::filesystem::path(image).parent_path() / L"conpty-backend",
                                     GlyphWidth::Console);
#else
        return system();
#endif
    }
    HRESULT create(COORD size, HANDLE input, HANDLE output, DWORD flags, HPCON* console) const {
        if (creation_flags_ && (flags & 0x18) && (flags & 0x18) != creation_flags_) return E_INVALIDARG;
        return api_->create(size, input, output, flags | creation_flags_, console);
    }
    HRESULT resize(HPCON console, COORD size) const { return api_->resize(console, size); }
    void close(HPCON console) const { api_->close(console); }
    bool side_by_side() const { return api_->module != nullptr; }
    DWORD creation_flags() const { return creation_flags_; }
};
} // namespace tf_conpty
