/* Include the application to exercise its helpers without opening a display. */
#define main nativessh_main
#include "../src/main.c"
#undef main
#include <assert.h>

static void expect_file(const char *path, const char *expected) {
    size_t len;
    char *text = read_text_file_or_empty(path, &len);
    assert(text && len == strlen(expected) && strcmp(text, expected) == 0);
    free(text);
}

int main(void) {
    alarm(15);
    assert(compare_dot_versions("1.1.0", "1.1.1") < 0);
    assert(compare_dot_versions("1.10.0", "1.2.0") > 0);
    assert(compare_dot_versions("1.1.1rc1", "1.1.1") == 0);
    assert(compare_dot_versions("1.1", "1.1.0") == 0);

#if defined(PLATFORM_NEXTUI)
    const char *platforms[] = { "tg5040", "tg5050", "my355", "h700" };
    const char *passwords[] = { "tina", "(empty - no password)", "rockchip", "root" };
    unsetenv("SYSTEM_PATH");
    setenv("PLATFORM", "unsupported", 1);
    assert(platform_config() == NULL);
    for (int i = 0; i < 4; ++i) {
        setenv("PLATFORM", platforms[i], 1);
        g_platform = platform_config();
        assert(g_platform && strcmp(g_platform->default_password, passwords[i]) == 0);
        assert(strstr(g_platform->userdata_root, platforms[i]));
    }
#endif

    /* Harmless commands let us execute the generated startup script. */
    ssh_platform_config config = {
        .check_cmd = "true", .enable_cmd = "true", .start_cmd = NULL,
        .disable_cmd = "true", .stop_cmd = "true",
    };
    g_platform = &config;
    char dir[] = "/tmp/nativessh-test-XXXXXX";
    assert(mkdtemp(dir));
    assert(chdir(dir) == 0);
    setenv("USERDATA_PATH", dir, 1);
    /* The dollar expression must stay literal, including embedded quotes. */
    setenv("SHARED_USERDATA_PATH", "./shared ' $(touch injected)", 1);
    const char *original = "#!/bin/sh\necho before >/dev/null\n";
    assert(write_text_file_atomic("auto.sh", original, strlen(original)) == 0);
    assert(persist_ssh_state(true) == 0);
    size_t len;
    char *once = read_text_file_or_empty("auto.sh", &len);
    assert(once && strncmp(once, original, strlen(original)) == 0);
    assert(upsert_auto_block() == 0);
    expect_file("auto.sh", once);
    free(once);
    assert(system("sh -eu auto.sh") == 0);
    assert(access("injected", F_OK) != 0);
    assert(persist_ssh_state(false) == 0);
    assert(system("sh -eu auto.sh") == 0);
    char state_path[PATH_MAX];
    assert(build_state_file(state_path, sizeof(state_path)));
    expect_file(state_path, SSH_STATE_DISABLED);

    const char *broken = "#!/bin/sh\n" SSH_AUTO_MARKER_BEGIN "echo keep-me\n";
    assert(write_text_file_atomic("auto.sh", broken, strlen(broken)) == 0);
    assert(upsert_auto_block() != 0);
    expect_file("auto.sh", broken);
    const char *stray_end = SSH_AUTO_MARKER_END "echo keep-me\n";
    assert(write_text_file_atomic("auto.sh", stray_end, strlen(stray_end)) == 0);
    assert(upsert_auto_block() != 0);
    expect_file("auto.sh", stray_end);

    assert(unlink("auto.sh") == 0);
    assert(symlink("auto.sh", "auto.sh") == 0);
    assert(upsert_auto_block() != 0); /* ELOOP is not a missing file. */
    struct stat st;
    assert(lstat("auto.sh", &st) == 0 && S_ISLNK(st.st_mode));
    assert(unlink("auto.sh") == 0);
    assert(unlink(state_path) == 0);
    char state_dir[PATH_MAX];
    assert(build_state_dir(state_dir, sizeof(state_dir)));
    assert(rmdir(state_dir) == 0);
    assert(rmdir(getenv("SHARED_USERDATA_PATH")) == 0);
    assert(chdir("/") == 0 && rmdir(dir) == 0);
    puts("PASS: firmware comparison, platform configuration, and startup persistence");
    return 0;
}
