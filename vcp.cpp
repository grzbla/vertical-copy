#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <chrono> // Added for microsecond resolution
#include <ctime>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #include <dirent.h> // Ensure you have a dirent shim for Windows (e.g. MinGW)
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

// --- Minimal FS Wrappers ---

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

// Recursive Mkdir (Required for deep timestamp trees)
bool fs_mkdir_p(const std::string& path) {
    std::string current_path = "";
    std::string target = path;
    
    // Handle Windows absolute paths (C:\) logic
    #ifdef _WIN32
    if (target.size() > 2 && target[1] == ':') {
        current_path += target.substr(0, 3);
        target = target.substr(3);
    }
    #endif

    size_t pos = 0;
    while ((pos = target.find(PATH_SEP)) != std::string::npos) {
        current_path += target.substr(0, pos);
        if (!current_path.empty() && !fs_exists(current_path)) {
            if (MKDIR(current_path.c_str()) != 0 && !fs_exists(current_path)) return false;
        }
        current_path += PATH_SEP;
        target.erase(0, pos + 1);
    }
    // Create the final leaf
    current_path += target;
    if (!current_path.empty() && !fs_exists(current_path)) {
        if (MKDIR(current_path.c_str()) != 0 && !fs_exists(current_path)) return false;
    }
    return true;
}

bool is_same_file(const std::string& p1, const std::string& p2) {
    struct stat s1, s2;
    if (stat(p1.c_str(), &s1) != 0 || stat(p2.c_str(), &s2) != 0) return false;
    if (s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino && s1.st_ino != 0) return true;
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
    if (attrs != INVALID_FILE_ATTRIBUTES) SetFileAttributesA(path.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
#endif
}

// --- IO Operations ---

bool fs_move(const std::string& src, const std::string& dst) {
#ifdef _WIN32
    return MoveFileExA(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
#else
    if (rename(src.c_str(), dst.c_str()) == 0) return true;
    FILE *in = fopen(src.c_str(), "rb"), *out = fopen(dst.c_str(), "wb");
    if (!in || !out) { if(in) fclose(in); return false; }
    char buf[16384]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out); unlink(src.c_str());
    return true;
#endif
}

bool fs_copy_file(const std::string& src, const std::string& dst) {
#ifdef _WIN32
    return CopyFileA(src.c_str(), dst.c_str(), FALSE);
#else
    FILE *in = fopen(src.c_str(), "rb"), *out = fopen(dst.c_str(), "wb");
    if (!in || !out) { if(in) fclose(in); return false; }
    char buf[16384]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out);
    return true;
#endif
}

// --- New Timestamp Logic ---

std::string get_deep_timestamp_path() {
    // 1. Get High Res Time
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    // 2. Extract Microseconds
    auto duration = now.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count() % 1000000;

    // 3. Local Time Struct
    struct tm ltm;
    #ifdef _WIN32
        localtime_s(&ltm, &time_t_now);
    #else
        localtime_r(&time_t_now, &ltm);
    #endif

    // 4. Build Path: yyyy/MM/dd/hh/mm/ss/uuuuuu
    char buf[256];
    snprintf(buf, sizeof(buf), "%04d%c%02d%c%02d%c%02d%c%02d%c%02d%c%06lld",
        ltm.tm_year + 1900, PATH_SEP,
        ltm.tm_mon + 1, PATH_SEP,
        ltm.tm_mday, PATH_SEP,
        ltm.tm_hour, PATH_SEP,
        ltm.tm_min, PATH_SEP,
        ltm.tm_sec, PATH_SEP,
        (long long)micros);
    
    return std::string(buf);
}

void archive_existing(const std::string& target, bool keep_source) {
    std::string parent = get_parent(target);
    std::string fname = get_filename(target);
    std::string v_root = fs_join(parent, ".v");
    
    // Ensure hidden root exists
    if (!fs_exists(v_root)) { MKDIR(v_root.c_str()); hide_dir(v_root); }

    // Logic: .v/[filename]/yyyy/MM/dd/hh/mm/ss/uuuuuu/
    std::string file_history_root = fs_join(v_root, fname);
    std::string time_path = get_deep_timestamp_path();
    std::string version_dir = fs_join(file_history_root, time_path);

    // Create the DEEP tree
    if (!fs_mkdir_p(version_dir)) { 
        fprintf(stderr, "Error creating vertical structure: %s\n", version_dir.c_str()); 
        exit(1); 
    }

    std::string archive_dest = fs_join(version_dir, fname);
    
    if (keep_source) {
        printf("  [Snapshot] %s -> .../%s\n", fname.c_str(), time_path.c_str());
        if (!fs_copy_file(target, archive_dest)) exit(1);
    } else {
        printf("  [Archive]  %s -> .../%s\n", fname.c_str(), time_path.c_str());
        if (!fs_move(target, archive_dest)) exit(1);
    }
}

// --- Recursive Logic (Unchanged from your snippet) ---

void recursive_copy(const std::string& src, const std::string& dst, bool flat_mode) {
    DIR* d = opendir(src.c_str());
    if (!d) return;

    if (!fs_exists(dst)) MKDIR(dst.c_str());

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        std::string name = ent->d_name;
        
        // IGNORE: .v, .git, .svn, .hg
        if (name == "." || name == ".." || name == ".v" || 
            name == ".git" || name == ".svn" || name == ".hg") continue;

        std::string src_path = fs_join(src, name);
        std::string dst_path = fs_join(dst, name);

        if (fs_is_dir(src_path)) {
            recursive_copy(src_path, dst_path, flat_mode);
        } else {
            if (fs_exists(dst_path)) {
                if (fs_is_dir(dst_path)) {
                      fprintf(stderr, "Skipping: %s (dest is directory)\n", dst_path.c_str());
                      continue;
                }
                
                bool same = is_same_file(src_path, dst_path);
                if (same) {
                    if (!flat_mode) {
                        archive_existing(dst_path, true); 
                    }
                    continue; 
                }

                if (!flat_mode) archive_existing(dst_path, false);
            }
            
            if (fs_copy_file(src_path, dst_path)) printf("OK: %s\n", dst_path.c_str());
            else fprintf(stderr, "Fail: %s\n", dst_path.c_str());
        }
    }
    closedir(d);
}

int main(int argc, char* argv[]) {
    // 1. Parse Args
    bool flat_mode = false;
    std::vector<std::string> args;
    
    for(int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-f" || arg == "--flat") flat_mode = true;
        else args.push_back(arg);
    }

    if (args.size() < 2) { 
        printf("Usage: vcp [SOURCE] [DESTINATION] [-f]\n");
        printf("  -f, --flat   Flat mode: Standard overwrite, no version history.\n");
        return 1; 
    }

    std::string src = args[0];
    std::string dest = args[1];

    if (!fs_exists(src)) { fprintf(stderr, "Error: Source not found: %s\n", src.c_str()); return 1; }

    // 2. Logic Dispatch
    if (fs_is_dir(src)) {
        if (fs_exists(dest) && fs_is_dir(dest)) dest = fs_join(dest, get_filename(src));
        
        if (flat_mode) printf("Mode: Flat Copy (Recursive)\n");
        recursive_copy(src, dest, flat_mode);
    } 
    else {
        if (fs_is_dir(dest)) dest = fs_join(dest, get_filename(src));
        
        if (fs_exists(dest)) {
            if (fs_is_dir(dest)) { fprintf(stderr, "Error: Destination is a directory.\n"); return 1; }
            
            bool same = is_same_file(src, dest);
            if (same) {
                if (flat_mode) return 0;
                archive_existing(dest, true);
                printf("OK: Snapshot created.\n");
                return 0;
            }

            if (!flat_mode) archive_existing(dest, false);
        }
        
        if (fs_copy_file(src, dest)) printf("OK: %s -> %s\n", get_filename(src).c_str(), dest.c_str());
        else { fprintf(stderr, "Error: Copy failed.\n"); return 1; }
    }
    return 0;
}
