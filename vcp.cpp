#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #include <dirent.h>
    #define PATH_SEP '\\'
    #define MKDIR(p) _mkdir(p)
    #ifndef S_IFDIR
        #define S_IFDIR _S_IFDIR
    #endif
#else
    #include <unistd.h>
    #include <dirent.h>
    #include <sys/types.h>
    #include <fcntl.h>
    #define PATH_SEP '/'
    #define MKDIR(p) mkdir(p, 0755)
#endif

// --- Helpers ---

bool fs_exists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

bool fs_is_dir(const std::string& path) {
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0) return false;
    return (buffer.st_mode & S_IFDIR);
}

std::string fs_join(const std::string& p1, const std::string& p2) {
    if (p1.empty()) return p2;
    if (p2.empty()) return p1;
    if (p1.back() == PATH_SEP) return p1 + p2;
    return p1 + std::string(1, PATH_SEP) + p2;
}

std::string get_filename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

std::string get_parent(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? "." : path.substr(0, pos);
}

bool is_same_file(const std::string& p1, const std::string& p2) {
    struct stat s1, s2;
    if (stat(p1.c_str(), &s1) != 0 || stat(p2.c_str(), &s2) != 0) return false;
    
    // Check device and inode (Linux/Unix/NTFS)
    if (s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino && s1.st_ino != 0) {
        return true;
    }
    
#ifdef _WIN32
    char abs1[MAX_PATH], abs2[MAX_PATH];
    if (_fullpath(abs1, p1.c_str(), MAX_PATH) && _fullpath(abs2, p2.c_str(), MAX_PATH)) {
        return _stricmp(abs1, abs2) == 0;
    }
#endif
    return false;
}

void hide_dir(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesA(path.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
    }
#endif
}

// --- IO Operations ---

bool fs_move(const std::string& src, const std::string& dst) {
#ifdef _WIN32
    return MoveFileExA(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
#else
    if (rename(src.c_str(), dst.c_str()) == 0) return true;
    // Fallback: Copy + Delete
    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(dst.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out);
    unlink(src.c_str());
    return true;
#endif
}

bool fs_copy_file(const std::string& src, const std::string& dst) {
#ifdef _WIN32
    return CopyFileA(src.c_str(), dst.c_str(), FALSE);
#else
    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(dst.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out);
    return true;
#endif
}

// --- Versioning Logic ---

int get_next_version(const std::string& dir) {
    int max_ver = 0;
    DIR* d = opendir(dir.c_str());
    if (!d) return 1;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        std::string full = fs_join(dir, name);
        if (fs_is_dir(full)) {
            if (name.size() > 1 && name[0] == 'v') {
                char* end;
                long val = strtol(name.c_str() + 1, &end, 10);
                if (*end == '\0' && val > max_ver) max_ver = (int)val;
            }
        }
    }
    closedir(d);
    return max_ver + 1;
}

std::string create_next_version_dir(const std::string& history_root) {
    if (!fs_exists(history_root)) MKDIR(history_root.c_str());
    int attempt = get_next_version(history_root);
    while (true) {
        std::string candidate = fs_join(history_root, "v" + std::to_string(attempt));
        if (MKDIR(candidate.c_str()) == 0) return candidate;
        if (!fs_exists(candidate)) return "";
        attempt++;
    }
}

void archive_existing(const std::string& target, bool keep_source) {
    std::string parent = get_parent(target);
    std::string fname = get_filename(target);
    std::string v_root = fs_join(parent, ".v");
    
    if (!fs_exists(v_root)) { MKDIR(v_root.c_str()); hide_dir(v_root); }

    std::string history_root = fs_join(v_root, fname);
    std::string version_dir = create_next_version_dir(history_root);

    if (version_dir.empty()) {
        fprintf(stderr, "Error: Could not create version directory for %s\n", target.c_str());
        exit(1);
    }

    std::string archive_dest = fs_join(version_dir, fname);
    
    if (keep_source) {
        printf("  [Snapshot] %s -> %s\n", fname.c_str(), version_dir.c_str());
        if (!fs_copy_file(target, archive_dest)) exit(1);
    } else {
        printf("  [Archive]  %s -> %s\n", fname.c_str(), version_dir.c_str());
        if (!fs_move(target, archive_dest)) exit(1);
    }
}

// --- Recursive Logic ---

void recursive_copy(const std::string& src, const std::string& dst) {
    DIR* d = opendir(src.c_str());
    if (!d) return;

    if (!fs_exists(dst)) MKDIR(dst.c_str());

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        if (name == ".v" || name == ".git") continue; // IGNORE HISTORY FOLDERS

        std::string src_path = fs_join(src, name);
        std::string dst_path = fs_join(dst, name);

        if (fs_is_dir(src_path)) {
            // Recurse
            recursive_copy(src_path, dst_path);
        } else {
            // Handle File
            if (fs_exists(dst_path)) {
                if (fs_is_dir(dst_path)) {
                     fprintf(stderr, "Skipping: %s is a directory in destination.\n", dst_path.c_str());
                     continue;
                }
                bool same = is_same_file(src_path, dst_path);
                archive_existing(dst_path, same);
                if (same) continue; // Snapshot done, nothing to copy
            }
            
            // Perform Copy
            if (fs_copy_file(src_path, dst_path)) {
                printf("OK: %s\n", dst_path.c_str());
            } else {
                fprintf(stderr, "Fail: %s\n", dst_path.c_str());
            }
        }
    }
    closedir(d);
}

// --- Main ---

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: vcp [-r] [SOURCE] [DESTINATION]\n");
        return 1;
    }

    bool recursive = false;
    int arg_idx = 1;
    if (std::string(argv[1]) == "-r" || std::string(argv[1]) == "-R") {
        recursive = true;
        arg_idx++;
    }

    if (argc <= arg_idx + 1) { // Check if we still have src and dst
         printf("Usage: vcp [-r] [SOURCE] [DESTINATION]\n");
         return 1;
    }

    std::string src = argv[arg_idx];
    std::string dest = argv[arg_idx + 1];

    if (!fs_exists(src)) {
        fprintf(stderr, "Error: Source not found: %s\n", src.c_str());
        return 1;
    }

    if (fs_is_dir(src)) {
        if (!recursive) {
            fprintf(stderr, "Error: Source is a directory (use -r).\n");
            return 1;
        }
        // If dest doesn't exist, we create it as the new root name
        // If dest exists and is dir, we copy src INTO dest (standard cp behavior)
        if (fs_exists(dest) && fs_is_dir(dest)) {
            dest = fs_join(dest, get_filename(src));
        }
        recursive_copy(src, dest);
    } else {
        // File Mode
        if (fs_is_dir(dest)) dest = fs_join(dest, get_filename(src));

        if (fs_exists(dest)) {
            if (fs_is_dir(dest)) {
                fprintf(stderr, "Error: Destination is a directory.\n");
                return 1;
            }
            bool same = is_same_file(src, dest);
            archive_existing(dest, same);
            if (same) {
                printf("OK: Snapshot created.\n");
                return 0;
            }
        }

        if (fs_copy_file(src, dest)) {
            printf("OK: %s -> %s\n", get_filename(src).c_str(), dest.c_str());
        } else {
            fprintf(stderr, "Error: Copy failed.\n");
            return 1;
        }
    }

    return 0;
}
