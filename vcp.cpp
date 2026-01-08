#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/stat.h>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #include <dirent.h>
    #define PATH_SEP '\\'
    #define MKDIR(p) _mkdir(p)
    // Handle MinGW stat macro differences
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

// --- Minimal FileSystem Wrapper ---

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

// --- Platform Specifics ---

void hide_dir(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesA(path.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
    }
#endif
}

// Atomic-ish move with fallback
bool fs_move(const std::string& src, const std::string& dst) {
#ifdef _WIN32
    return MoveFileExA(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
#else
    if (rename(src.c_str(), dst.c_str()) == 0) return true;
    
    // Fallback for EXDEV (cross-device)
    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(dst.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    unlink(src.c_str());
    return true;
#endif
}

// Simple copy (overwrite)
bool fs_copy(const std::string& src, const std::string& dst) {
#ifdef _WIN32
    return CopyFileA(src.c_str(), dst.c_str(), FALSE); 
#else
    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(dst.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    return true;
#endif
}

// --- Logic ---

int get_next_version(const std::string& dir) {
    int max_ver = 0;
    DIR* d = opendir(dir.c_str());
    if (!d) return 1;

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        std::string name = ent->d_name;
        
        // Skip . and ..
        if (name == "." || name == "..") continue;

        // FIX: Construct full path and use stat() instead of missing d_type
        std::string full_path = fs_join(dir, name);
        
        if (fs_is_dir(full_path)) {
            // Check if name matches v[Number]
            if (name.size() > 1 && name[0] == 'v') {
                char* end;
                long val = strtol(name.c_str() + 1, &end, 10);
                if (*end == '\0' && val > max_ver) {
                    max_ver = (int)val;
                }
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
        if (MKDIR(candidate.c_str()) == 0) {
            return candidate;
        }
        if (!fs_exists(candidate)) {
            return ""; // Real error
        }
        attempt++;
    }
}

// Added bool 'keep_source' defaults to false (standard move behavior)
void archive_existing(const std::string& target, bool keep_source) {
    std::string parent = get_parent(target);
    std::string fname = get_filename(target);

    std::string v_root = fs_join(parent, ".v");
    if (!fs_exists(v_root)) {
        MKDIR(v_root.c_str());
        hide_dir(v_root);
    }

    std::string history_root = fs_join(v_root, fname);
    std::string version_dir = create_next_version_dir(history_root);
    
    if (version_dir.empty()) {
        fprintf(stderr, "Error: Could not create version directory.\n");
        exit(1);
    }

    std::string archive_dest = fs_join(version_dir, fname);
    
    if (keep_source) {
        // SNAPSHOT MODE: Copy to archive, leave original intact
        printf("  >> Snapshotting: %s -> %s\n", fname.c_str(), version_dir.c_str());
        if (!fs_copy(target, archive_dest)) {
            fprintf(stderr, "Error: Failed to snapshot file.\n");
            exit(1);
        }
    } else {
        // STANDARD MODE: Move to archive (prepare for overwrite)
        printf("  >> Archiving: %s -> %s\n", fname.c_str(), version_dir.c_str());
        if (!fs_move(target, archive_dest)) {
            fprintf(stderr, "Error: Failed to move file to archive.\n");
            exit(1);
        }
    }
}

bool is_same_file(const std::string& p1, const std::string& p2) {
    struct stat s1, s2;
    if (stat(p1.c_str(), &s1) != 0 || stat(p2.c_str(), &s2) != 0) return false;

    // Check strict device/inode equality (Linux/NTFS)
    if (s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino && s1.st_ino != 0) {
        return true;
    }

#ifdef _WIN32
    // Windows fallback: Case-insensitive absolute path string comparison
    // (Handles FAT32 where inodes might be unreliable)
    char abs1[MAX_PATH], abs2[MAX_PATH];
    if (_fullpath(abs1, p1.c_str(), MAX_PATH) && _fullpath(abs2, p2.c_str(), MAX_PATH)) {
        return _stricmp(abs1, abs2) == 0; 
    }
#endif
    return false;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: vcp [SOURCE] [DESTINATION]\n");
        return 1;
    }

    std::string src = argv[1];
    std::string dest = argv[2];

    if (!fs_exists(src)) {
        fprintf(stderr, "Error: Source not found: %s\n", src.c_str());
        return 1;
    }

    // Handle directory destination
    if (fs_is_dir(dest)) {
        dest = fs_join(dest, get_filename(src));
    }

    // CHECK IDENTITY BEFORE ACTING
    if (is_same_file(src, dest)) {
        printf("Source and Destination are the same.\n");
        // Just snapshot the file to history and exit. 
        // Do not attempt to overwrite itself.
        archive_existing(dest, true); // true = keep source
        printf("OK: Snapshot created.\n");
        return 0;
    }

    if (fs_exists(dest)) {
        if (fs_is_dir(dest)) {
            fprintf(stderr, "Error: Destination is a directory.\n");
            return 1;
        }
        // Standard behavior: Move old file away
        archive_existing(dest, false); // false = move source
    }

    if (fs_copy(src, dest)) {
        printf("OK: %s -> %s\n", get_filename(src).c_str(), dest.c_str());
    } else {
        fprintf(stderr, "Error: Copy failed.\n");
        return 1;
    }

    return 0;
}