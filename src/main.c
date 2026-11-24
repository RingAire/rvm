/*
 * main.c — RingEcho Version Manager (rvm)
 *
 * nvm-like: manage RingEcho compiler versions
 *   rvm install <version>        install a specific version
 *   rvm use <version>            switch to a specific version
 *   rvm list                     list installed versions
 *   rvm current                  show current version
 *   rvm uninstall <version>      remove a version
 *   rvm remote                   list available versions
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifndef RVM_VERSION
#define RVM_VERSION "0.1.1"
#endif


/* Standalone project: rvm intentionally depends on no compiler headers. */
#if defined(_WIN32) || defined(_WIN64)
#define RVM_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#define RVM_PLATFORM_MACOS 1
#elif defined(__linux__)
#define RVM_PLATFORM_LINUX 1
#else
#error "unsupported host platform"
#endif

#if defined(RVM_PLATFORM_WINDOWS)
#include <direct.h>
#include <process.h>
#include <windows.h>
#include <io.h>
#include <sys/stat.h>
#define RVM_MKDIR(path) _mkdir(path)
#define RVM_SEP '\\'
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <dirent.h>
#define RVM_MKDIR(path) mkdir((path), 0755)
#define RVM_SEP '/'
#endif

#define RVM_DIR ".rvm"
#define RVM_VERSIONS_DIR ".rvm/versions"
#define RVM_CURRENT ".rvm/current"
#define RVM_CURRENT_VERSION ".rvm/current.version"
#define RVM_DEFAULT_VERSION "0.3.0"

/* Distribution contract. Both bases share the relative path
 *   /v<version>/<asset>[.zip|.tar.gz]
 * so switching the host never changes the asset naming:
 *   primary  https://ringecho-lang.org/dist/v0.3.0/ringecho-0.3.0-windows-x64.zip
 *   fallback https://github.com/RingAire/ringecho-lang-c/releases/download/v0.3.0/...
 * The site is authoritative once it serves the tree; until then `install`
 * falls back to GitHub releases and says so. RVM_DIST_BASE replaces the
 * primary host (mirror or local test server); an explicit override never
 * falls back. `rvm remote` reads <base>/index.txt (one version per line). */
#define RVM_SITE_BASE "https://ringecho-lang.org/dist"
#define RVM_GITHUB_BASE "https://github.com/RingAire/ringecho-lang-c/releases/download"
#define RVM_INDEX_FILE "index.txt"

static void print_usage(void) {
    printf("RingEcho Version Manager (rvm) v" RVM_VERSION "\n");
    printf("Usage:\n");
    printf("  rvm install <version>        Install a RingEcho version\n");
    printf("  rvm use <version>            Switch to a version\n");
    printf("  rvm list                     List installed versions\n");
    printf("  rvm current                  Show current version\n");
    printf("  rvm uninstall <version>      Remove a version\n");
    printf("  rvm remote                   List available versions\n");
}

static bool valid_version(const char *v) {
    if (!v || !v[0]) return false;
    /* path traversal forms must never reach the versions directory */
    if (strcmp(v, ".") == 0 || strcmp(v, "..") == 0) return false;
    for (const unsigned char *p = (const unsigned char *)v; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '.' || *p == '-' || *p == '_')) return false;
    }
    return true;
}

/* Release asset naming: ringecho-<version>-<platform>-<arch>.zip */
static const char *rvm_platform(void) {
#if defined(RVM_PLATFORM_WINDOWS)
    return "windows";
#elif defined(RVM_PLATFORM_MACOS)
    return "macos";
#else
    return "linux";
#endif
}

static const char *rvm_arch(void) {
#if defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "x64";
#endif
}

static void rvm_payload(const char *version, char *out, size_t cap) {
    snprintf(out, cap, "ringecho-%s-%s-%s", version, rvm_platform(), rvm_arch());
}

/* Portable archive suffix: Windows releases ship .zip, Unix releases .tar.gz
 * (see scripts/ci/package.py). Both are uploaded by the release workflow. */
static const char *rvm_asset_suffix(void) {
#if defined(RVM_PLATFORM_WINDOWS)
    return ".zip";
#else
    return ".tar.gz";
#endif
}

static bool make_path(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len == 0 || len >= sizeof(tmp)) return false;
    if (tmp[len-1] == '/' || tmp[len-1] == '\\') tmp[len-1] = '\0';

    /* Skip the part of an absolute path that must already exist: the root on
     * POSIX, the drive (or UNC share) on Windows. A real Windows profile such
     * as C:\Users\name used to make component creation start at "C:". */
    char *start = tmp;
#if defined(RVM_PLATFORM_WINDOWS)
    if (((tmp[0] >= 'A' && tmp[0] <= 'Z') || (tmp[0] >= 'a' && tmp[0] <= 'z')) &&
        tmp[1] == ':') {
        start = tmp + 2;
        if (*start == '/' || *start == '\\') start++;
    } else if (tmp[0] == '\\' && tmp[1] == '\\') {
        char *server_end = strpbrk(tmp + 2, "\\/");
        char *share_end = server_end ? strpbrk(server_end + 1, "\\/") : NULL;
        start = share_end ? share_end + 1 : tmp + len;
    }
#else
    if (*start == '/') start++;
#endif
    for (char *p = start; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            if (RVM_MKDIR(tmp) != 0 && errno != EEXIST) return false;
            *p = '/';
        }
    }
    if (RVM_MKDIR(tmp) != 0 && errno != EEXIST) return false;
    return true;
}

static int run_program(const char *prog, char *const argv[]) {
#if defined(RVM_PLATFORM_WINDOWS)
    intptr_t rc = _spawnvp(_P_WAIT, prog, (const char *const *)argv);
    return (rc < 0) ? -1 : (int)rc;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(prog, argv);
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

/* Active distribution host: RVM_DIST_BASE wins, otherwise the project site. */
static const char *rvm_dist_base(void) {
    const char *base = getenv("RVM_DIST_BASE");
    return (base && *base) ? base : RVM_SITE_BASE;
}

static bool rvm_dist_overridden(void) {
    const char *base = getenv("RVM_DIST_BASE");
    return base && *base;
}

/* Download <base>/v<version>/<asset> into `destination`.
 * `asset` already carries the platform suffix. */
static bool rvm_download(const char *base, const char *version,
                         const char *asset, const char *destination) {
    char url[1024];
    int written = snprintf(url, sizeof(url), "%s/v%s/%s", base, version, asset);
    if (written < 0 || (size_t)written >= sizeof(url)) return false;
    /* Release CDNs occasionally reset the connection; retry transient
     * failures (404 keeps failing fast, so a missing version is still clear). */
    char *curl_argv[] = { "curl", "-fsSL", "--retry", "3", "--retry-delay", "1",
                          "--connect-timeout", "20", "-o", (char *)destination, url, NULL };
    return run_program("curl", curl_argv) == 0;
}

/* Run a program with `dir` as its working directory. Used for extraction so
 * tar receives a relative archive name: GNU tar on Git Bash treats a leading
 * drive letter ("D:...") as a remote host. */
static int run_program_in(const char *prog, char *const argv[], const char *dir) {
#if defined(RVM_PLATFORM_WINDOWS)
    char saved[1024];
    if (!_getcwd(saved, sizeof(saved))) return -1;
    if (_chdir(dir) != 0) return -1;
    int rc = run_program(prog, argv);
    _chdir(saved);
    return rc;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (chdir(dir) != 0) _exit(127);
        execvp(prog, argv);
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

static void get_home_dir(char *out, size_t cap) {
#if defined(RVM_PLATFORM_WINDOWS)
    /* USERPROFILE is already the full home; HOMEPATH is relative to HOMEDRIVE
     * and must never be appended to USERPROFILE. */
    const char *home = getenv("USERPROFILE");
    if (home && *home) { snprintf(out, cap, "%s", home); return; }
    const char *drive = getenv("HOMEDRIVE");
    const char *path = getenv("HOMEPATH");
    if (drive && path) { snprintf(out, cap, "%s%s", drive, path); return; }
    home = getenv("HOME");
    if (home && *home) { snprintf(out, cap, "%s", home); return; }
    snprintf(out, cap, ".");
#else
    const char *home = getenv("HOME");
    if (home && *home) { snprintf(out, cap, "%s", home); return; }
    snprintf(out, cap, ".");
#endif
}

static void ensure_rvm_dir(void) {
    char home[512];
    get_home_dir(home, sizeof(home));
    home[sizeof(home) - 1] = '\0';
    char dir[600];
    snprintf(dir, sizeof(dir), "%s/%s", home, RVM_DIR);
    RVM_MKDIR(dir);
    snprintf(dir, sizeof(dir), "%s/%s", home, RVM_VERSIONS_DIR);
    RVM_MKDIR(dir);
}

static void get_versions_dir(char *out, size_t cap) {
    char home[512];
    get_home_dir(home, sizeof(home));
    snprintf(out, cap, "%s/%s", home, RVM_VERSIONS_DIR);
}

static void get_current_link(char *out, size_t cap) {
    char home[512];
    get_home_dir(home, sizeof(home));
    snprintf(out, cap, "%s/%s", home, RVM_CURRENT);
}

static void get_current_version_path(char *out, size_t cap) {
    char home[512];
    get_home_dir(home, sizeof(home));
    snprintf(out, cap, "%s/%s", home, RVM_CURRENT_VERSION);
}

#if defined(RVM_PLATFORM_WINDOWS)
static int remove_directory_recursive(const char *path);
#endif

/* Remove a version directory, including a partially extracted one. Used both
 * by `uninstall` and by `install` failure rollback. */
static void discard_dir(const char *path) {
#if defined(RVM_PLATFORM_WINDOWS)
    remove_directory_recursive(path);
#else
    char *rm_argv[] = { "rm", "-rf", (char *)path, NULL };
    run_program("rm", rm_argv);
#endif
}

/* Read the version selected by `rvm use`. Returns false when no marker exists. */
static bool read_current_version(char *out, size_t cap) {
    char marker[600];
    get_current_version_path(marker, sizeof(marker));
    FILE *f = fopen(marker, "r");
    if (!f) return false;
    bool ok = cap > 1 && fgets(out, (int)cap, f) != NULL;
    fclose(f);
    if (!ok) return false;
    out[strcspn(out, "\r\n")] = '\0';
    return true;
}

static int cmd_install(const char *version) {
    if (!version) {
        fprintf(stderr, "usage: rvm install <version>\n");
        return 1;
    }
    if (!valid_version(version)) {
        fprintf(stderr, "invalid version '%s'\n", version);
        return 1;
    }

    ensure_rvm_dir();

    char versions_dir[600];
    get_versions_dir(versions_dir, sizeof(versions_dir));

    char version_dir[700];
    snprintf(version_dir, sizeof(version_dir), "%s/%s", versions_dir, version);

    char payload[256];
    rvm_payload(version, payload, sizeof(payload));

    char bin_dir[1000];
    snprintf(bin_dir, sizeof(bin_dir), "%s/%s/bin", version_dir, payload);

    struct stat st;
    if (stat(version_dir, &st) == 0) {
        if (stat(bin_dir, &st) == 0) {
            printf("Version %s is already installed\n", version);
            return 0;
        }
        /* A previous attempt failed between mkdir and extraction. Recover
         * instead of reporting a usable installation. */
        printf("Removing incomplete installation of %s...\n", version);
        discard_dir(version_dir);
    }

    char asset[300];
    snprintf(asset, sizeof(asset), "%s%s", payload, rvm_asset_suffix());

    if (!make_path(version_dir)) {
        fprintf(stderr, "cannot create version directory\n");
        return 1;
    }

    printf("Installing RingEcho %s...\n", version);
    char archive_path[900];
    snprintf(archive_path, sizeof(archive_path), "%s/.download%s",
             version_dir, rvm_asset_suffix());

    bool downloaded = rvm_download(rvm_dist_base(), version, asset, archive_path);
    if (!downloaded && !rvm_dist_overridden()) {
        /* The project site is the primary source once its dist tree is live.
         * Until then (or during an outage) use the GitHub release assets. */
        printf("Distribution site unavailable; falling back to GitHub releases.\n");
        downloaded = rvm_download(RVM_GITHUB_BASE, version, asset, archive_path);
    }
    if (!downloaded) {
        printf("Failed to download version %s\n", version);
        printf("Check: %s or https://github.com/RingAire/ringecho-lang-c/releases\n",
               RVM_SITE_BASE);
        discard_dir(version_dir);
        return 1;
    }

    char archive_name[64];
    snprintf(archive_name, sizeof(archive_name), ".download%s", rvm_asset_suffix());
#if defined(RVM_PLATFORM_WINDOWS)
    /* The Windows archive is a zip. Git Bash's GNU tar cannot read zip and
     * misparses drive-letter paths, so use the system bsdtar explicitly. */
    char tar_path[1024];
    const char *system_root = getenv("SystemRoot");
    snprintf(tar_path, sizeof(tar_path), "%s\\System32\\tar.exe",
             (system_root && *system_root) ? system_root : "C:\\Windows");
    char *extract_argv[] = { tar_path, "-xf", archive_name, NULL };
    int rc = run_program_in(extract_argv[0], extract_argv, version_dir);
#else
    char *extract_argv[] = { "tar", "-xzf", archive_name, NULL };
    int rc = run_program_in("tar", extract_argv, version_dir);
#endif
    remove(archive_path);
    if (rc != 0) {
        printf("Failed to extract version %s (need 'tar')\n", version);
        discard_dir(version_dir);
        return 1;
    }

    if (stat(bin_dir, &st) != 0) {
        printf("unexpected package layout for %s\n", version);
        discard_dir(version_dir);
        return 1;
    }

    printf("Installed: RingEcho %s\n", version);
    printf("Activate with: rvm use %s\n", version);
    return 0;
}

static int cmd_use(const char *version) {
    if (!version) {
        fprintf(stderr, "usage: rvm use <version>\n");
        return 1;
    }
    if (!valid_version(version)) {
        fprintf(stderr, "invalid version '%s'\n", version);
        return 1;
    }

    char versions_dir[600];
    get_versions_dir(versions_dir, sizeof(versions_dir));

    char version_dir[700];
    snprintf(version_dir, sizeof(version_dir), "%s/%s", versions_dir, version);

    char payload[256];
    rvm_payload(version, payload, sizeof(payload));
    char bin_dir[1000];
    snprintf(bin_dir, sizeof(bin_dir), "%s/%s/bin", version_dir, payload);

    struct stat st;
    if (stat(bin_dir, &st) != 0) {
        fprintf(stderr, "version %s is not installed (run 'rvm install %s')\n", version, version);
        return 1;
    }

    char current_link[600];
    get_current_link(current_link, sizeof(current_link));
#if defined(RVM_PLATFORM_WINDOWS)
    /* Directory junctions do not require administrator rights, unlike
     * symbolic links. mklink needs backslash paths: it treats '/' as a switch. */
    char link_win[600], target_win[1000];
    snprintf(link_win, sizeof(link_win), "%s", current_link);
    snprintf(target_win, sizeof(target_win), "%s", bin_dir);
    for (char *p = link_win; *p; p++) if (*p == '/') *p = '\\';
    for (char *p = target_win; *p; p++) if (*p == '/') *p = '\\';
    DeleteFileA(link_win);
    RemoveDirectoryA(link_win);
    {
        char *link_argv[] = { "cmd", "/c", "mklink", "/J", link_win, target_win, NULL };
        if (run_program("cmd", link_argv) != 0) {
            fprintf(stderr, "cannot create junction '%s' -> '%s'\n", link_win, target_win);
            return 1;
        }
    }
#else
    unlink(current_link);
    if (symlink(bin_dir, current_link) != 0) {
        fprintf(stderr, "cannot create symlink '%s' -> '%s': %s\n",
                current_link, bin_dir, strerror(errno));
        return 1;
    }
#endif

    char marker[600];
    get_current_version_path(marker, sizeof(marker));
    FILE *mf = fopen(marker, "w");
    if (mf) { fprintf(mf, "%s\n", version); fclose(mf); }

    printf("Now using RingEcho %s\n", version);
#if defined(RVM_PLATFORM_WINDOWS)
    printf("Add to PATH: set PATH=%%USERPROFILE%%\\.rvm\\current;%%PATH%%\n");
#else
    printf("Add to PATH: export PATH=\"%s:$PATH\"\n", current_link);
#endif
    return 0;
}

static int cmd_list(void) {
    char versions_dir[600];
    get_versions_dir(versions_dir, sizeof(versions_dir));

    struct stat st;
    if (stat(versions_dir, &st) != 0) {
        printf("No versions installed\n");
        printf("Run 'rvm install <version>' to install\n");
        return 0;
    }

    printf("Installed versions:\n");
#if defined(RVM_PLATFORM_WINDOWS)
    {
        WIN32_FIND_DATAA fd;
        char pattern[700];
        snprintf(pattern, sizeof(pattern), "%s\\*", versions_dir);
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.cFileName[0] == '.') continue;
                printf("  %s\n", fd.cFileName);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
#else
    DIR *d = opendir(versions_dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.') continue;
            printf("  %s\n", de->d_name);
        }
        closedir(d);
    }
#endif
    return 0;
}

static int cmd_current(void) {
    char version[256];
    if (!read_current_version(version, sizeof(version))) {
        printf("No version selected (using system default: %s)\n", RVM_DEFAULT_VERSION);
        return 0;
    }

    char current_link[600];
    get_current_link(current_link, sizeof(current_link));
    struct stat st;
    if (stat(current_link, &st) != 0) {
        /* The marker survived but the link/target is gone (manual deletion or
         * an interrupted uninstall). Say so instead of claiming a usable tool. */
        printf("Current: %s (not linked; run 'rvm use %s')\n", version, version);
        return 0;
    }
    printf("Current: %s\n", version);
    return 0;
}

#if defined(RVM_PLATFORM_WINDOWS)
static int remove_directory_recursive(const char *path) {
    WIN32_FIND_DATAA fd;
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        if (RemoveDirectoryA(path)) return 0;
        if (DeleteFileA(path)) return 0;
        return -1;
    }
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char child[1024];
        snprintf(child, sizeof(child), "%s\\%s", path, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            remove_directory_recursive(child);
        } else {
            DeleteFileA(child);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    RemoveDirectoryA(path);
    return 0;
}
#endif

static int cmd_uninstall(const char *version) {
    if (!version) {
        fprintf(stderr, "usage: rvm uninstall <version>\n");
        return 1;
    }
    if (!valid_version(version)) {
        fprintf(stderr, "invalid version '%s'\n", version);
        return 1;
    }

    char versions_dir[600];
    get_versions_dir(versions_dir, sizeof(versions_dir));

    char version_dir[700];
    snprintf(version_dir, sizeof(version_dir), "%s/%s", versions_dir, version);

    struct stat st;
    if (stat(version_dir, &st) != 0) {
        printf("Version %s is not installed\n", version);
        return 1;
    }

#if defined(RVM_PLATFORM_WINDOWS)
    remove_directory_recursive(version_dir);
#else
    char *rm_argv[] = { "rm", "-rf", version_dir, NULL };
    run_program("rm", rm_argv);
#endif
    printf("Removed: RingEcho %s\n", version);

    /* Do not leave a dangling `current` selection behind. */
    char current_version[256];
    if (read_current_version(current_version, sizeof(current_version)) &&
        strcmp(current_version, version) == 0) {
        char current_link[600];
        get_current_link(current_link, sizeof(current_link));
#if defined(RVM_PLATFORM_WINDOWS)
        DeleteFileA(current_link);
        RemoveDirectoryA(current_link);   /* junction: removes the link only */
#else
        unlink(current_link);
#endif
        char marker[600];
        get_current_version_path(marker, sizeof(marker));
        remove(marker);
        printf("No version selected now; run 'rvm use <version>'.\n");
    }
    return 0;
}

/* Print <base>/index.txt (one version per line). Returns false when the index
 * cannot be fetched or is empty, so the caller can fall back. */
static bool print_remote_index(const char *base) {
    char url[900];
    int written = snprintf(url, sizeof(url), "%s/%s", base, RVM_INDEX_FILE);
    if (written < 0 || (size_t)written >= sizeof(url)) return false;

    char home[512];
    get_home_dir(home, sizeof(home));
    char temp[700];
    snprintf(temp, sizeof(temp), "%s/%s/.index.txt", home, RVM_DIR);

    char *curl_argv[] = { "curl", "-fsSL", "--retry", "3", "--retry-delay", "1",
                          "--connect-timeout", "20", "-o", temp, url, NULL };
    if (run_program("curl", curl_argv) != 0) {
        remove(temp);
        return false;
    }

    FILE *f = fopen(temp, "r");
    if (!f) {
        remove(temp);
        return false;
    }
    char line[128];
    bool any = false;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        printf("  %s\n", line);
        any = true;
    }
    fclose(f);
    remove(temp);
    return any;
}

static int cmd_remote(void) {
    ensure_rvm_dir();
    const char *base = rvm_dist_base();
    printf("Available versions (%s):\n", base);
    if (print_remote_index(base)) return 0;

    /* No published index (site not deployed yet, or offline): report the known
     * releases and where to look, instead of pretending to know the list. */
    printf("  0.1.0  (initial release)\n");
    printf("  0.3.0  (current stable)\n");
    printf("\nDistribution: %s\n", RVM_SITE_BASE);
    printf("Check: https://github.com/RingAire/ringecho-lang-c/releases\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "install") == 0) return cmd_install(argc > 2 ? argv[2] : NULL);
    if (strcmp(cmd, "use") == 0)     return cmd_use(argc > 2 ? argv[2] : NULL);
    if (strcmp(cmd, "list") == 0 || strcmp(cmd, "ls") == 0)
                                     return cmd_list();
    if (strcmp(cmd, "current") == 0) return cmd_current();
    if (strcmp(cmd, "uninstall") == 0)
                                     return cmd_uninstall(argc > 2 ? argv[2] : NULL);
    if (strcmp(cmd, "remote") == 0)  return cmd_remote();
    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
        printf("rvm v" RVM_VERSION "\n");
        return 0;
    }

    print_usage();
    return 1;
}
