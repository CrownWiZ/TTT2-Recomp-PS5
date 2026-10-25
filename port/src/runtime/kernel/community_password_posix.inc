// Per-installation AES-GCM key in the application's private save directory (0600).
// Android's save directory is app-private; Linux uses the owning user's data directory.
bool PasswordKey(unsigned char (&key)[32]) {
    auto path = std::filesystem::path(FilePath()); std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    path += ".key";
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0 && errno == ENOENT) {
        unsigned char fresh[32]; if (!CommunityRandom(fresh, sizeof(fresh))) return false;
        fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0) { bool ok = write(fd, fresh, sizeof(fresh)) == sizeof(fresh) && fsync(fd) == 0; close(fd); OPENSSL_cleanse(fresh, sizeof(fresh)); if (!ok) return false; }
        fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    }
    if (fd < 0) return false;
    struct stat st{}; bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == getuid() && !(st.st_mode & 0077) && st.st_size == 32 && read(fd, key, 32) == 32;
    close(fd); return ok;
}
std::string Protect(const std::string& plain) {
    if (plain.empty()) return {};
    unsigned char key[32], nonce[12]; if (!PasswordKey(key) || !CommunityRandom(nonce, sizeof(nonce))) return {};
    std::vector<unsigned char> blob(12 + plain.size() + 16); std::memcpy(blob.data(), nonce, 12);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new(); int n = 0, end = 0;
    bool ok = ctx && EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, nonce) == 1 && EVP_EncryptUpdate(ctx, blob.data() + 12, &n, reinterpret_cast<const unsigned char*>(plain.data()), int(plain.size())) == 1 && EVP_EncryptFinal_ex(ctx, blob.data() + 12 + n, &end) == 1 && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, blob.data() + 12 + plain.size()) == 1;
    EVP_CIPHER_CTX_free(ctx); OPENSSL_cleanse(key, sizeof(key)); if (!ok) return {};
    static const char hex[] = "0123456789abcdef"; std::string out = "aesgcm:";
    for (auto c : blob) { out += hex[c >> 4]; out += hex[c & 15]; } return out;
}
std::string Unprotect(const std::string& stored) {
    if (stored.rfind("aesgcm:", 0) || stored.size() < 7 + 56 || (stored.size() - 7) % 2) return {};
    std::vector<unsigned char> blob; auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (size_t i = 7; i < stored.size(); i += 2) { int a = digit(stored[i]), b = digit(stored[i+1]); if (a < 0 || b < 0) return {}; blob.push_back((a << 4) | b); }
    unsigned char key[32]; if (!PasswordKey(key)) return {};
    std::string out(blob.size() - 28, '\0'); EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new(); int n = 0, end = 0;
    bool ok = ctx && EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, blob.data()) == 1 && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()), &n, blob.data() + 12, int(out.size())) == 1 && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, blob.data() + blob.size() - 16) == 1 && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(out.data()) + n, &end) == 1;
    EVP_CIPHER_CTX_free(ctx); OPENSSL_cleanse(key, sizeof(key)); if (!ok) return {}; return out;
}
