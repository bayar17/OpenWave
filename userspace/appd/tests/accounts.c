/* Exercise the production account transaction without touching host accounts. */
#include "../isolation.c"
#include <glib/gstdio.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>

static const char initial[] = "root:x:0:0:root:/root:/bin/sh\nocean:x:1000:1000:Ocean:/Data/Users/ocean:/bin/sh\n";
static char *fixture, *accounts_path, *identities_path;
static int accounts, identities;

const char *appd_legacy_id(App *app)
{
    return !strcmp(app->id, "com.openwave.ocean") ? "browser" : NULL;
}

static void seed(int directory, const char *name, const char *text, mode_t mode)
{
    int fd = openat(directory, name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(write(fd, text, strlen(text)), ==, (ssize_t)strlen(text));
    g_assert_cmpint(fchown(fd, 0, 0), ==, 0);
    g_assert_cmpint(fchmod(fd, mode), ==, 0);
    close(fd);
}

static void setup(void)
{
    fixture = g_build_filename(g_getenv("OPENWAVE_ACCOUNT_TEST_ROOT"), "fixture-XXXXXX", NULL);
    g_assert_nonnull(g_mkdtemp(fixture));
    accounts_path = g_build_filename(fixture, "accounts", NULL);
    identities_path = g_build_filename(fixture, "identities", NULL);
    g_assert_cmpint(mkdir(accounts_path, 0755), ==, 0);
    g_assert_cmpint(mkdir(identities_path, 0700), ==, 0);
    g_assert_cmpint(chown(accounts_path, 0, 0), ==, 0);
    g_assert_cmpint(chown(identities_path, 0, 0), ==, 0);
    accounts = open(accounts_path, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    identities = open(identities_path, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    g_assert_cmpint(accounts, >=, 0); g_assert_cmpint(identities, >=, 0);
    seed(accounts, "passwd", initial, 0644);
}

static void teardown(void)
{
    close(accounts); close(identities);
    g_free(identities_path); g_free(accounts_path); g_free(fixture);
}

static uid_t provision(const char *id, const char *home, GError **error)
{
    Manager manager = {.uid = 1000};
    App app = {.id = (char *)id};
    return provision_uid_at(&manager, &app, home, accounts, identities, error);
}

static char *read_passwd(void)
{
    char *text; gsize length; GError *error = NULL;
    g_assert_true(read_identity_file(accounts, "passwd", 0644, FALSE, &text, &length, &error));
    g_assert_no_error(error); return text;
}

static void create_and_update(void)
{
    setup(); GError *error = NULL;
    g_assert_cmpuint(provision("com.example.first", "/first", &error), ==, 200000);
    g_assert_no_error(error);
    g_assert_cmpuint(provision("com.example.second", "/second", &error), ==, 200001);
    g_assert_cmpuint(provision("com.example.first", "/moved", &error), ==, 200000);
    g_assert_no_error(error);
    char *text = read_passwd();
    g_assert_nonnull(strstr(text, "owapp200000:x:200000:200000:OpenWave application:/moved:/bin/false\n"));
    g_assert_nonnull(strstr(text, "owapp200001:x:200001:200001:OpenWave application:/second:/bin/false\n"));
    g_assert_null(strstr(text, ":/first:"));
    g_assert_true(g_str_has_prefix(text, initial)); g_free(text);
    struct stat st;
    g_assert_cmpint(fstatat(accounts, "passwd", &st, AT_SYMLINK_NOFOLLOW), ==, 0);
    g_assert_true(protected_metadata(&st, 0644));
    g_assert_cmpint(fstatat(identities, "users.ini", &st, AT_SYMLINK_NOFOLLOW), ==, 0);
    g_assert_true(protected_metadata(&st, 0600));
    teardown();
}

static void migrate_legacy(void)
{
    setup();
    seed(identities, "users.ini", "[Applications]\n1000:browser=200042\n", 0600);
    GError *error = NULL;
    g_assert_cmpuint(provision("com.openwave.ocean", "/migrated", &error), ==, 200042);
    g_assert_no_error(error);
    char *contents; gsize length;
    g_assert_true(read_identity_file(identities, "users.ini", 0600, FALSE, &contents, &length, &error));
    g_assert_nonnull(strstr(contents, "1000:com.openwave.ocean=200042"));
    g_assert_null(strstr(contents, "1000:browser="));
    g_free(contents); teardown();
}

static void unsafe_files(void)
{
    const char *names[] = {"passwd", "users.ini", ".lock"};
    for (guint i = 0; i < G_N_ELEMENTS(names); ++i) {
        setup();
        int directory = i == 1 ? identities : accounts;
        seed(accounts, "sentinel", "untouched", 0644);
        if (!i) g_assert_cmpint(unlinkat(directory, names[i], 0), ==, 0);
        char *target = g_build_filename(accounts_path, "sentinel", NULL);
        g_assert_cmpint(symlinkat(target, directory, names[i]), ==, 0);
        GError *error = NULL;
        g_assert_cmpuint(provision("com.example.unsafe", "/unsafe", &error), ==, 0);
        g_assert_nonnull(error); g_clear_error(&error);
        struct stat st;
        g_assert_cmpint(fstatat(directory, names[i], &st, AT_SYMLINK_NOFOLLOW), ==, 0);
        g_assert_true(S_ISLNK(st.st_mode));
        char *contents; gsize length;
        g_assert_true(g_file_get_contents(target, &contents, &length, NULL));
        g_assert_cmpstr(contents, ==, "untouched");
        g_free(contents); g_free(target); teardown();
    }
    setup();
    g_assert_cmpint(linkat(accounts, "passwd", accounts, "alias", 0), ==, 0);
    g_assert_cmpuint(provision("com.example.hardlink", "/unsafe", NULL), ==, 0);
    g_assert_cmpint(unlinkat(accounts, "alias", 0), ==, 0);
    g_assert_cmpint(fchmodat(accounts, "passwd", 0666, 0), ==, 0);
    g_assert_cmpuint(provision("com.example.writable", "/unsafe", NULL), ==, 0);
    g_assert_cmpint(fchmodat(accounts, "passwd", 0644, 0), ==, 0);
    g_assert_cmpint(fchownat(accounts, "passwd", 42, 0, 0), ==, 0);
    g_assert_cmpuint(provision("com.example.owner", "/unsafe", NULL), ==, 0);
    teardown();
}

static void reject_partial_write(void)
{
    setup();
    pid_t pid = fork(); g_assert_cmpint(pid, >=, 0);
    if (!pid) {
        struct rlimit limit = {.rlim_cur = 32, .rlim_max = 32};
        signal(SIGXFSZ, SIG_IGN);
        if (setrlimit(RLIMIT_FSIZE, &limit)) _exit(2);
        _exit(update_account_home(accounts, 200000, "/too-long", NULL) ? 3 : 0);
    }
    int status; g_assert_cmpint(waitpid(pid, &status, 0), ==, pid);
    g_assert_true(WIFEXITED(status)); g_assert_cmpint(WEXITSTATUS(status), ==, 0);
    char *text = read_passwd(); g_assert_cmpstr(text, ==, initial); g_free(text);
    DIR *dir = fdopendir(dup(accounts)); struct dirent *entry;
    while ((entry = readdir(dir))) g_assert_false(g_str_has_prefix(entry->d_name, ".passwd."));
    closedir(dir); teardown();
}

static void concurrent_allocators(void)
{
    setup();
    enum { CHILDREN = 8 }; pid_t children[CHILDREN];
    int gate[2]; g_assert_cmpint(pipe(gate), ==, 0);
    for (guint i = 0; i < CHILDREN; ++i) {
        children[i] = fork(); g_assert_cmpint(children[i], >=, 0);
        if (!children[i]) {
            close(gate[1]); char token;
            if (read(gate[0], &token, 1) != 1) _exit(2);
            char *id = g_strdup_printf("com.example.parallel%u", i);
            uid_t uid = provision(id, "/parallel", NULL);
            _exit(uid >= 200000 && uid < 200000 + CHILDREN ? 0 : 3);
        }
    }
    close(gate[0]);
    g_assert_cmpint(write(gate[1], "12345678", CHILDREN), ==, CHILDREN); close(gate[1]);
    for (guint i = 0; i < CHILDREN; ++i) {
        int status; g_assert_cmpint(waitpid(children[i], &status, 0), ==, children[i]);
        g_assert_true(WIFEXITED(status)); g_assert_cmpint(WEXITSTATUS(status), ==, 0);
    }
    char *contents; gsize length;
    g_assert_true(read_identity_file(identities, "users.ini", 0600, FALSE, &contents, &length, NULL));
    GKeyFile *map = g_key_file_new();
    g_assert_true(g_key_file_load_from_data(map, contents, length, 0, NULL)); g_free(contents);
    gboolean seen[CHILDREN] = {FALSE};
    for (guint i = 0; i < CHILDREN; ++i) {
        char *key = g_strdup_printf("1000:com.example.parallel%u", i);
        guint64 uid = g_key_file_get_uint64(map, "Applications", key, NULL); g_free(key);
        g_assert_cmpuint(uid, >=, 200000); g_assert_cmpuint(uid, <, 200000 + CHILDREN);
        g_assert_false(seen[uid - 200000]); seen[uid - 200000] = TRUE;
    }
    g_key_file_unref(map); teardown();
}

static void reject_injected_home(void)
{
    setup();
    g_assert_cmpuint(provision("com.example.inject", "/home\nother:x:0:0::/:/bin/sh", NULL), ==, 0);
    char *text = read_passwd(); g_assert_cmpstr(text, ==, initial); g_free(text);
    teardown();
}

static void landlock_readonly(void)
{
    if (syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION) < 1) {
        g_test_skip("host Landlock unavailable"); return;
    }
    setup();
    pid_t pid = fork(); g_assert_cmpint(pid, >=, 0);
    if (!pid) {
        const guint64 read = LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR;
        struct landlock_ruleset_attr policy = {.handled_access_fs = read | LANDLOCK_ACCESS_FS_WRITE_FILE};
        int ruleset = syscall(SYS_landlock_create_ruleset, &policy, sizeof(policy), 0);
        if (ruleset < 0 || allow_path(ruleset, accounts_path, read, TRUE)
            || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)
            || syscall(SYS_landlock_restrict_self, ruleset, 0)) _exit(2);
        close(ruleset);
        int file = openat(accounts, "passwd", O_RDONLY | O_CLOEXEC);
        if (file < 0) _exit(3);
        close(file);
        file = openat(accounts, "passwd", O_WRONLY | O_CLOEXEC);
        _exit(file < 0 && errno == EACCES ? 0 : 4);
    }
    int status; g_assert_cmpint(waitpid(pid, &status, 0), ==, pid);
    g_assert_true(WIFEXITED(status)); g_assert_cmpint(WEXITSTATUS(status), ==, 0);
    teardown();
}

static void trusted_storage_owner(void)
{
    setup();
    char *data = g_build_filename(fixture, "trusted-data", NULL);
    g_assert_cmpint(mkdir(data, 0700), ==, 0);
    g_assert_cmpint(chown(data, 0, 0), ==, 0);
    int directory = open(data, O_RDONLY | O_DIRECTORY);
    seed(directory, "config", "trusted settings", 0600);
    close(directory);
    Instance instance = {.data = data, .uid = 1000, .storage_changed = TRUE};
    g_assert_true(prepare_data_owner(&instance));
    struct stat st;
    g_assert_cmpint(lstat(data, &st), ==, 0); g_assert_cmpuint(st.st_uid, ==, 1000);
    char *config = g_build_filename(data, "config", NULL);
    g_assert_cmpint(lstat(config, &st), ==, 0); g_assert_cmpuint(st.st_uid, ==, 1000);
    g_assert_cmpint(chown(data, 200001, 200001), ==, 0);
    g_assert_false(prepare_data_owner(&instance));
    char *link = g_build_filename(fixture, "redirect", NULL);
    g_assert_cmpint(symlink(data, link), ==, 0);
    instance.data = link;
    g_assert_false(prepare_data_owner(&instance));
    g_free(config); g_free(link); g_free(data); teardown();
}

static int sandbox_probe(void)
{
    /* Run in the real bwrap namespace with a relative /etc/passwd symlink. */
    struct passwd *account = getpwuid(200000);
    if (!account || strcmp(account->pw_name, "owapp200000")) return 2;
    int fd = open("/var/lib/openwave-accounts/passwd", O_WRONLY | O_CLOEXEC);
    if (fd >= 0 || errno != EROFS) return 3;
    fd = open("/etc/passwd", O_WRONLY | O_CLOEXEC);
    if (fd >= 0 || errno != EROFS) return 4;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--sandbox-probe")) return sandbox_probe();
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/accounts/trusted-storage-owner", trusted_storage_owner);
    g_test_add_func("/accounts/create-update", create_and_update);
    g_test_add_func("/accounts/legacy-mapping", migrate_legacy);
    g_test_add_func("/accounts/unsafe-files", unsafe_files);
    g_test_add_func("/accounts/partial-write", reject_partial_write);
    g_test_add_func("/accounts/concurrent-allocators", concurrent_allocators);
    g_test_add_func("/accounts/injected-home", reject_injected_home);
    g_test_add_func("/accounts/landlock-readonly", landlock_readonly);
    return g_test_run();
}
