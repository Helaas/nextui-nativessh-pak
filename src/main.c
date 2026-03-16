#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_WIDGETS_IMPLEMENTATION
#include "apostrophe_widgets.h"

#include <ifaddrs.h>
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Platform-specific constants (set via -DPLATFORM_xxx compiler flag)
 * ------------------------------------------------------------------------- */

#if defined(PLATFORM_TG5040)
    #define SSH_CHECK_CMD        "pidof sshd > /dev/null 2>&1"
    #define SSH_ENABLE_CMD       "/usr/trimui/bin/systemval enablessh 1"
    #define SSH_START_CMD        "/usr/sbin/sshd"
    #define SSH_DISABLE_CMD      "/usr/trimui/bin/systemval enablessh 0"
    #define SSH_STOP_CMD         "killall -9 sshd"
    #define SSH_DEFAULT_PASSWORD "tina"
    #define VERSION_FILE         "/etc/version"
    #define MIN_VERSION          "1.1.1"
    #define NEEDS_VERSION_CHECK  1
    #define VERSION_FORMAT_DOT   1
    #define VERSION_FORMAT_DATE  0
#elif defined(PLATFORM_TG5050)
    #define SSH_CHECK_CMD        "pidof sshd > /dev/null 2>&1"
    #define SSH_ENABLE_CMD       "/usr/trimui/bin/systemval enablessh 1"
    #define SSH_START_CMD        "/usr/sbin/sshd"
    #define SSH_DISABLE_CMD      "/usr/trimui/bin/systemval enablessh 0"
    #define SSH_STOP_CMD         "killall -9 sshd"
    #define SSH_DEFAULT_PASSWORD "(empty - no password)"
    #define NEEDS_VERSION_CHECK  0
    #define VERSION_FORMAT_DOT   0
    #define VERSION_FORMAT_DATE  0
#elif defined(PLATFORM_MY355)
    #define SSH_CHECK_CMD        "pidof dropbear > /dev/null 2>&1"
    #define SSH_ENABLE_CMD       "/etc/init.d/S50dropbear start"
    #define SSH_START_CMD        NULL
    #define SSH_DISABLE_CMD      "/etc/init.d/S50dropbear stop"
    #define SSH_STOP_CMD         "killall dropbear"
    #define SSH_DEFAULT_PASSWORD "rockchip"
    #define VERSION_FILE         "/usr/miyoo/version"
    #define MIN_VERSION          "20250228"
    #define NEEDS_VERSION_CHECK  1
    #define VERSION_FORMAT_DOT   0
    #define VERSION_FORMAT_DATE  1
#elif defined(PLATFORM_MAC)
    #define SSH_CHECK_CMD        "pgrep -x sshd > /dev/null 2>&1"
    #define SSH_ENABLE_CMD       NULL
    #define SSH_START_CMD        NULL
    #define SSH_DISABLE_CMD      NULL
    #define SSH_STOP_CMD         NULL
    #define SSH_DEFAULT_PASSWORD "tina"
    #define NEEDS_VERSION_CHECK  0
    #define VERSION_FORMAT_DOT   0
    #define VERSION_FORMAT_DATE  0
#else
    #error "No platform defined. Use -DPLATFORM_TG5040, -DPLATFORM_TG5050, -DPLATFORM_MY355, or -DPLATFORM_MAC"
#endif

#define SSH_PORT "22"

#if !defined(PLATFORM_MAC)
    #if defined(PLATFORM_TG5040)
        #define DEVICE_USERDATA_ROOT_FALLBACK "/mnt/SDCARD/.userdata/tg5040"
    #elif defined(PLATFORM_TG5050)
        #define DEVICE_USERDATA_ROOT_FALLBACK "/mnt/SDCARD/.userdata/tg5050"
    #elif defined(PLATFORM_MY355)
        #define DEVICE_USERDATA_ROOT_FALLBACK "/mnt/SDCARD/.userdata/my355"
    #else
        #define DEVICE_USERDATA_ROOT_FALLBACK "/mnt/SDCARD/.userdata"
    #endif

    #define SHARED_USERDATA_ROOT_FALLBACK "/mnt/SDCARD/.userdata/shared"
    #define SSH_STATE_DIR_NAME            "nativessh"
    #define SSH_STATE_ENABLED             "enabled\n"
    #define SSH_STATE_DISABLED            "disabled\n"
    #define SSH_AUTO_MARKER_BEGIN         "# >>> nativessh-managed >>>\n"
    #define SSH_AUTO_MARKER_END           "# <<< nativessh-managed <<<\n"
#endif

/* ---------------------------------------------------------------------------
 * SSH status
 * ------------------------------------------------------------------------- */

static bool is_ssh_running(void) {
    return system(SSH_CHECK_CMD) == 0;
}

#if !defined(PLATFORM_MAC)
static const char *get_env_or_default(const char *name, const char *fallback) {
    const char *value = getenv(name);
    return (value && *value) ? value : fallback;
}

static bool build_state_dir(char *buf, size_t buflen) {
    return snprintf(buf, buflen, "%s/%s",
        get_env_or_default("SHARED_USERDATA_PATH", SHARED_USERDATA_ROOT_FALLBACK),
        SSH_STATE_DIR_NAME) < (int)buflen;
}

static bool build_state_file(char *buf, size_t buflen) {
    char state_dir[PATH_MAX];

    if (!build_state_dir(state_dir, sizeof(state_dir)))
        return false;

    return snprintf(buf, buflen, "%s/state", state_dir) < (int)buflen;
}

static bool build_auto_path(char *buf, size_t buflen) {
    return snprintf(buf, buflen, "%s/auto.sh",
        get_env_or_default("USERDATA_PATH", DEVICE_USERDATA_ROOT_FALLBACK)) < (int)buflen;
}

static int ensure_dir_exists(const char *path) {
    char tmp[PATH_MAX];
    size_t len = strlen(path);

    if (len == 0 || len >= sizeof(tmp))
        return -1;

    memcpy(tmp, path, len + 1);

    if (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';

    for (char *p = tmp + 1; *p; ++p) {
        if (*p != '/')
            continue;

        *p = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;

    return 0;
}

static int ensure_parent_dir_exists(const char *path) {
    char parent[PATH_MAX];
    char *slash;

    if (strlen(path) >= sizeof(parent))
        return -1;

    strcpy(parent, path);
    slash = strrchr(parent, '/');
    if (!slash)
        return 0;
    if (slash == parent) {
        slash[1] = '\0';
        return 0;
    }

    *slash = '\0';
    return ensure_dir_exists(parent);
}

static int write_text_file_atomic(const char *path, const char *content, size_t len) {
    char temp_path[PATH_MAX];
    FILE *file;

    if (snprintf(temp_path, sizeof(temp_path), "%s.tmp", path) >= (int)sizeof(temp_path))
        return -1;

    if (ensure_parent_dir_exists(path) != 0)
        return -1;

    file = fopen(temp_path, "wb");
    if (!file)
        return -1;

    if (len > 0 && fwrite(content, 1, len, file) != len) {
        fclose(file);
        unlink(temp_path);
        return -1;
    }

    if (fclose(file) != 0) {
        unlink(temp_path);
        return -1;
    }

    if (rename(temp_path, path) != 0) {
        unlink(temp_path);
        return -1;
    }

    return 0;
}

static char *read_text_file_or_empty(const char *path, size_t *out_len) {
    FILE *file = fopen(path, "rb");
    char *buffer;
    long file_size;

    if (!file) {
        buffer = malloc(1);
        if (!buffer)
            return NULL;
        buffer[0] = '\0';
        *out_len = 0;
        return buffer;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = malloc((size_t)file_size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    if (file_size > 0 && fread(buffer, 1, (size_t)file_size, file) != (size_t)file_size) {
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[file_size] = '\0';
    fclose(file);
    *out_len = (size_t)file_size;
    return buffer;
}

static int upsert_auto_block(void) {
    char auto_path[PATH_MAX];
    char state_path[PATH_MAX];
    char block[(PATH_MAX * 2) + 512];
    const char *start_cmd = SSH_START_CMD;
    char start_line[256] = "";
    char *existing;
    char *begin;
    char *end;
    char *new_content;
    size_t existing_len;
    size_t block_len;
    size_t prefix_len;
    size_t suffix_len;
    size_t extra_newline = 0;
    int result;

    if (!build_auto_path(auto_path, sizeof(auto_path)) ||
        !build_state_file(state_path, sizeof(state_path)))
        return -1;

    if (start_cmd) {
        if (snprintf(start_line, sizeof(start_line), "    %s > /dev/null 2>&1\n", start_cmd) >= (int)sizeof(start_line))
            return -1;
    }

    block_len = (size_t)snprintf(block, sizeof(block),
        "%s"
        "STATE_FILE=\"%s\"\n"
        "state=\"\"\n"
        "if [ -f \"$STATE_FILE\" ]; then\n"
        "    state=$(cat \"$STATE_FILE\" 2>/dev/null)\n"
        "fi\n"
        "if [ \"$state\" = \"enabled\" ]; then\n"
        "    %s > /dev/null 2>&1\n"
        "%s"
        "elif [ \"$state\" = \"disabled\" ]; then\n"
        "    %s > /dev/null 2>&1\n"
        "    %s > /dev/null 2>&1\n"
        "fi\n"
        "%s",
        SSH_AUTO_MARKER_BEGIN,
        state_path,
        SSH_ENABLE_CMD,
        start_line,
        SSH_DISABLE_CMD,
        SSH_STOP_CMD,
        SSH_AUTO_MARKER_END);
    if (block_len >= sizeof(block))
        return -1;

    existing = read_text_file_or_empty(auto_path, &existing_len);
    if (!existing)
        return -1;

    begin = strstr(existing, SSH_AUTO_MARKER_BEGIN);
    end = begin ? strstr(begin, SSH_AUTO_MARKER_END) : NULL;

    if (begin && end) {
        prefix_len = (size_t)(begin - existing);
        end += strlen(SSH_AUTO_MARKER_END);
        if (*end == '\r')
            ++end;
        if (*end == '\n')
            ++end;
        suffix_len = existing_len - (size_t)(end - existing);
    } else if (begin) {
        prefix_len = (size_t)(begin - existing);
        suffix_len = 0;
    } else {
        prefix_len = existing_len;
        suffix_len = 0;
        if (existing_len > 0 && existing[existing_len - 1] != '\n')
            extra_newline = 1;
    }

    new_content = malloc(prefix_len + extra_newline + block_len + suffix_len + 1);
    if (!new_content) {
        free(existing);
        return -1;
    }

    memcpy(new_content, existing, prefix_len);
    if (extra_newline)
        new_content[prefix_len] = '\n';
    memcpy(new_content + prefix_len + extra_newline, block, block_len);
    if (suffix_len > 0)
        memcpy(new_content + prefix_len + extra_newline + block_len,
            existing + existing_len - suffix_len,
            suffix_len);
    new_content[prefix_len + extra_newline + block_len + suffix_len] = '\0';

    result = write_text_file_atomic(auto_path, new_content,
        prefix_len + extra_newline + block_len + suffix_len);
    if (result == 0)
        chmod(auto_path, 0755);

    free(new_content);
    free(existing);
    return result;
}

static void persist_ssh_state(bool enable) {
    char state_dir[PATH_MAX];
    char state_path[PATH_MAX];
    const char *state = enable ? SSH_STATE_ENABLED : SSH_STATE_DISABLED;

    if (!build_state_dir(state_dir, sizeof(state_dir)) ||
        !build_state_file(state_path, sizeof(state_path))) {
        fprintf(stderr, "Failed to resolve SSH persistence paths\n");
        return;
    }

    if (ensure_dir_exists(state_dir) != 0) {
        fprintf(stderr, "Failed to create SSH state directory: %s\n", state_dir);
        return;
    }

    if (write_text_file_atomic(state_path, state, strlen(state)) != 0) {
        fprintf(stderr, "Failed to write SSH state file: %s\n", state_path);
        return;
    }

    if (upsert_auto_block() != 0)
        fprintf(stderr, "Failed to update auto.sh for SSH persistence\n");
}
#endif

/* ---------------------------------------------------------------------------
 * IP address detection
 * ------------------------------------------------------------------------- */

static bool get_ip_address(char *buf, size_t buflen) {
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1) {
        snprintf(buf, buflen, "Unknown");
        return false;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;
        if (!(ifa->ifa_flags & IFF_UP))
            continue;

#if defined(PLATFORM_MAC)
        /* On macOS, prefer en* interfaces (Wi-Fi/Ethernet) */
        if (strncmp(ifa->ifa_name, "en", 2) != 0)
            continue;
#endif

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, buf, (socklen_t)buflen);
        freeifaddrs(ifaddr);
        return true;
    }

    freeifaddrs(ifaddr);
    snprintf(buf, buflen, "No network");
    return false;
}

/* ---------------------------------------------------------------------------
 * Version checking
 * ------------------------------------------------------------------------- */

#if VERSION_FORMAT_DOT
static int compare_dot_versions(const char *a, const char *b) {
    /* Compare dot-separated numeric versions (e.g. "1.0.1" vs "1.1.1") */
    const char *pa = a, *pb = b;

    while (*pa || *pb) {
        long va = 0, vb = 0;

        if (*pa) {
            va = strtol(pa, (char **)&pa, 10);
            if (*pa == '.') pa++;
        }
        if (*pb) {
            vb = strtol(pb, (char **)&pb, 10);
            if (*pb == '.') pb++;
        }

        if (va < vb) return -1;
        if (va > vb) return 1;
    }
    return 0;
}
#endif

#if NEEDS_VERSION_CHECK
static bool check_os_version(void) {
    FILE *f = fopen(VERSION_FILE, "r");
    if (!f)
        return true; /* can't read version, proceed anyway */

    char version[64] = {0};
    if (!fgets(version, sizeof(version), f)) {
        fclose(f);
        return true;
    }
    fclose(f);

    /* Trim trailing whitespace/newline */
    size_t len = strlen(version);
    while (len > 0 && (version[len - 1] == '\n' || version[len - 1] == '\r' || version[len - 1] == ' '))
        version[--len] = '\0';

    if (len == 0)
        return true;

    bool version_ok = true;

#if VERSION_FORMAT_DOT
    version_ok = compare_dot_versions(version, MIN_VERSION) >= 0;
#elif VERSION_FORMAT_DATE
    /* Version file contains YYYYMMDD... (e.g. "20250228101926").
     * Compare first 8 characters against MIN_VERSION ("20250228"). */
    version_ok = len >= 8 && strncmp(version, MIN_VERSION, 8) >= 0;
#endif

    if (!version_ok) {
        char msg[256];
        snprintf(msg, sizeof(msg),
            "Firmware version %s detected.\n\n"
            "Native SSH requires version %s or higher.\n"
            "Please update your firmware.",
            version, MIN_VERSION);

        ap_footer_item footer[] = {
            { .button = AP_BTN_B, .label = "Quit", .is_confirm = false },
        };

        ap_message_opts opts = {
            .message = msg,
            .image_path = NULL,
            .footer = footer,
            .footer_count = 1,
        };
        ap_confirm_result result;
        ap_confirmation(&opts, &result);
        return false;
    }

    return true;
}
#endif

/* ---------------------------------------------------------------------------
 * SSH toggle (runs in background thread)
 * ------------------------------------------------------------------------- */

static void run_cmd(const char *cmd) {
    if (cmd) system(cmd);
}

static int enable_ssh(void *userdata) {
    (void)userdata;

#if defined(PLATFORM_MAC)
    usleep(1000000);
    return 0;
#else
    run_cmd(SSH_ENABLE_CMD);
    run_cmd(SSH_START_CMD);

#if !defined(PLATFORM_MAC)
    persist_ssh_state(true);
#endif

    /* Poll until running (up to 5 seconds) */
    for (int i = 0; i < 10; i++) {
        if (is_ssh_running())
            return 0;
        usleep(500000);
    }
    return 0;
#endif
}

static int disable_ssh(void *userdata) {
    (void)userdata;

#if defined(PLATFORM_MAC)
    usleep(1000000);
    return 0;
#else
    run_cmd(SSH_DISABLE_CMD);
    run_cmd(SSH_STOP_CMD);

#if !defined(PLATFORM_MAC)
    persist_ssh_state(false);
#endif

    /* Poll until stopped (up to 5 seconds) */
    for (int i = 0; i < 10; i++) {
        if (!is_ssh_running())
            return 0;
        usleep(500000);
    }
    return 0;
#endif
}

static void toggle_ssh(bool enable) {
    ap_process_opts opts = {
        .message = enable ? "Enabling SSH..." : "Disabling SSH...",
        .show_progress = false,
    };
    ap_process_message(&opts, enable ? enable_ssh : disable_ssh, NULL);
}

/* ---------------------------------------------------------------------------
 * Status screen
 * ------------------------------------------------------------------------- */

typedef enum {
    ACTION_QUIT,
    ACTION_TOGGLE,
} screen_action;

static screen_action show_status_screen(bool ssh_running) {
    char ip_buf[64];
    char connect_buf[128];

    /* Build info pairs */
    ap_detail_info_pair pairs_disabled[] = {
        { .key = "Status",  .value = "Disabled" },
        { .key = "IP",      .value = "-" },
        { .key = "Port",    .value = "-" },
    };

    get_ip_address(ip_buf, sizeof(ip_buf));
    snprintf(connect_buf, sizeof(connect_buf), "ssh root@%s -p %s", ip_buf, SSH_PORT);

    ap_detail_info_pair pairs_enabled[] = {
        { .key = "Status",   .value = "Enabled" },
        { .key = "IP",       .value = ip_buf },
        { .key = "Port",     .value = SSH_PORT },
        { .key = "Connect",  .value = connect_buf },
        { .key = "Password", .value = SSH_DEFAULT_PASSWORD },
    };

    ap_detail_section status_section = {
        .type = AP_SECTION_INFO,
        .title = "SSH",
    };
    ap_detail_section warning_section = {
        .type = AP_SECTION_DESCRIPTION,
        .title = "Warning",
        .description =
            "The credentials shown here are stock defaults. Other custom firmware "
            "or manual SSH setup can change the username and password.",
    };
    ap_detail_section enabled_sections[2];
    ap_detail_section *sections = &status_section;
    int section_count = 1;

    if (ssh_running) {
        status_section.info_pairs = pairs_enabled;
        status_section.info_count = sizeof(pairs_enabled) / sizeof(pairs_enabled[0]);

        enabled_sections[0] = status_section;
        enabled_sections[1] = warning_section;
        sections = enabled_sections;
        section_count = 2;
    } else {
        status_section.info_pairs = pairs_disabled;
        status_section.info_count = sizeof(pairs_disabled) / sizeof(pairs_disabled[0]);
    }

    ap_footer_item footer[] = {
        { .button = AP_BTN_B, .label = "Quit",    .is_confirm = false },
        { .button = AP_BTN_A, .label = ssh_running ? "Disable" : "Enable", .is_confirm = true },
    };

    ap_color key_col = ap_get_theme()->text;
    TTF_Font *medium_font = ap_get_font(AP_FONT_MEDIUM);

    ap_detail_opts opts = {
        .title = "Native SSH",
        .sections = sections,
        .section_count = section_count,
        .footer = footer,
        .footer_count = 2,
        .center_title = true,
        .show_section_separator = true,
        .key_color = &key_col,
        .body_font = medium_font,
        .section_title_font = medium_font,
        .key_font = medium_font,
    };

    ap_detail_result result;
    ap_detail_screen(&opts, &result);

    if (result.action == AP_DETAIL_ACTION)
        return ACTION_TOGGLE;

    return ACTION_QUIT;
}

/* ---------------------------------------------------------------------------
 * Main loop
 * ------------------------------------------------------------------------- */

static void run_app(void) {
    for (;;) {
        bool ssh_running = is_ssh_running();
        screen_action action = show_status_screen(ssh_running);

        switch (action) {
        case ACTION_TOGGLE:
            toggle_ssh(!ssh_running);
            break;
        case ACTION_QUIT:
            return;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    ap_config cfg = {0};
    cfg.window_title = "Native SSH";
    cfg.font_path = AP_PLATFORM_IS_DEVICE ? NULL : "third_party/apostrophe/res/font.ttf";
    cfg.log_path = ap_resolve_log_path("nativessh");
    cfg.is_nextui = AP_PLATFORM_IS_DEVICE;
    cfg.cpu_speed = AP_CPU_SPEED_MENU;

    if (ap_init(&cfg) != AP_OK) {
        fprintf(stderr, "Failed to initialise Apostrophe: %s\n", ap_get_error());
        return 1;
    }

#if NEEDS_VERSION_CHECK
    if (!check_os_version()) {
        ap_quit();
        return 0;
    }
#endif

    run_app();
    ap_quit();
    return 0;
}
