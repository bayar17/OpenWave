#include "appdeploy/appdeploy.hpp"
#include "util.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>

extern "C" {
#include <rpm/header.h>
#include <rpm/rpmarchive.h>
#include <rpm/rpmbase64.h>
#include <rpm/rpmcli.h>
#include <rpm/rpmds.h>
#include <rpm/rpmfi.h>
#include <rpm/rpmio.h>
#include <rpm/rpmkeyring.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmmacro.h>
#include <rpm/rpmpgp.h>
#include <rpm/rpmstring.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmts.h>
}

namespace appdeploy {

const char *trust_name(Trust trust)
{
    switch (trust) {
    case Trust::Application: return "application";
    case Trust::System: return "system";
    case Trust::Publisher: return "publisher";
    default: return "untrusted";
    }
}

const char *class_name(PackageClass klass) { return klass == PackageClass::System ? "system" : "application"; }

namespace {
std::string captured_log;
int capture_log(rpmlogRec record, rpmlogCallbackData)
{
    if (rpmlogRecPriority(record) <= RPMLOG_WARNING) {
        if (!captured_log.empty()) captured_log += "; ";
        captured_log += rpmlogRecMessage(record);
        while (!captured_log.empty() && (captured_log.back() == '\n' || captured_log.back() == ' ')) captured_log.pop_back();
    }
    return 0;
}

std::once_flag rpm_once;
void init_rpm(const Config &config)
{
    std::call_once(rpm_once, [&config] {
        rpmlogSetMask(RPMLOG_UPTO(RPMLOG_WARNING));
        rpmlogSetCallback(capture_log, nullptr);
        rpmDefineMacro(nullptr, ("_dbpath " + config.state_dir + "/rpmdb").c_str(), 0);
        rpmDefineMacro(nullptr, "_pkgverify_level all", 0);
        rpmDefineMacro(nullptr, "_pkgverify_flags 0x0", 0);
        if (rpmReadConfigFiles(nullptr, nullptr)) throw Error("cannot read the RPM configuration");
    });
}

struct Ts {
    rpmts ts = rpmtsCreate();
    ~Ts() { if (ts) rpmtsFree(ts); }
};

bool signature_matches(int fd, const std::string &name, rpmPubkey key)
{
    const int duplicate = ::dup(fd);
    if (duplicate < 0 || ::lseek(duplicate, 0, SEEK_SET) < 0) { if (duplicate >= 0) ::close(duplicate); throw Error("cannot read " + name); }
    FD_t file = Fdopen(fdDup(duplicate), "r.ufdio");
    ::close(duplicate);
    if (!file || Ferror(file)) { if (file) Fclose(file); throw Error("cannot open " + name); }
    Ts transaction;
    rpmKeyring keyring = rpmKeyringNew();
    rpmKeyringAddKey(keyring, key);
    rpmtsSetKeyring(transaction.ts, keyring);
    rpmKeyringFree(keyring);
    rpmtsSetVfyLevel(transaction.ts, RPMSIG_VERIFIABLE_TYPE);
    rpmtsSetVfyFlags(transaction.ts, RPMVSF_DEFAULT);
    struct rpmQVKArguments_s qva;
    std::memset(&qva, 0, sizeof(qva));
    captured_log.clear();
    const bool ok = rpmVerifySignatures(&qva, transaction.ts, file, name.c_str()) == 0;
    Fclose(file);
    return ok;
}

rpmPubkey parse_armored_key(const std::string &armored)
{
    uint8_t *packets = nullptr;
    size_t length = 0;
    if (armored.empty() || armored.size() > 65536 || pgpParsePkts(armored.c_str(), &packets, &length) != PGPARMOR_PUBKEY) {
        free(packets);
        return nullptr;
    }
    rpmPubkey key = rpmPubkeyNew(packets, length);
    free(packets);
    return key;
}

std::string key_user_id(rpmPubkey key)
{
    pgpDigParams params = rpmPubkeyPgpDigParams(key);
    const char *identity = params ? pgpDigParamsUserID(params) : nullptr;
    return identity ? identity : "";
}

std::string hex_key_id(rpmPubkey key)
{
    pgpDigParams params = rpmPubkeyPgpDigParams(key);
    if (!params) return {};
    char *hex = rpmhex(pgpDigParamsSignID(params), PGP_KEYID_LEN);
    std::string result = hex ? hex : "";
    free(hex);
    return result;
}
}

struct KeyStore::Impl {
    Config config;
    std::vector<Key> keys;
    struct Loaded { rpmPubkey key = nullptr; Trust klass; std::string id; };
    std::vector<Loaded> loaded;
    ~Impl() { for (auto &entry : loaded) rpmPubkeyFree(entry.key); }
};

KeyStore::KeyStore(const Config &config) : p(new Impl)
{
    p->config = config;
    init_rpm(config);
    for (const auto &[directory, klass] : {std::pair{"system", Trust::System}, std::pair{"application", Trust::Application}}) {
        const std::string path = config.path(config.keys_dir + "/" + directory);
        const int fd = util::open_directory_nofollow(path);
        if (fd < 0) continue;
        struct stat dir_stat;
        if (::fstat(fd, &dir_stat) || dir_stat.st_uid != config.owner_uid || (dir_stat.st_mode & 022)) {
            ::close(fd);
            throw Error("key directory " + path + " is not root-owned and private to root (owner "
                        + std::to_string(dir_stat.st_uid) + ", expected " + std::to_string(config.owner_uid)
                        + ", mode " + std::to_string(dir_stat.st_mode & 07777) + ")");
        }
        DIR *stream = ::fdopendir(fd);
        std::vector<std::string> names;
        while (dirent *entry = ::readdir(stream)) {
            const std::string name = entry->d_name;
            if (util::ends_with(name, ".asc") || util::ends_with(name, ".gpg") || util::ends_with(name, ".pub"))
                names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        for (const auto &name : names) {
            struct stat st;
            if (::fstatat(dirfd(stream), name.c_str(), &st, AT_SYMLINK_NOFOLLOW) || !S_ISREG(st.st_mode)
                || st.st_uid != config.owner_uid || (st.st_mode & 022))
                throw Error("key file " + path + "/" + name + " is not a root-owned regular file");
            rpmPubkey key = rpmPubkeyRead((path + "/" + name).c_str());
            if (!key) throw Error("cannot parse key file " + path + "/" + name);
            const std::string id = hex_key_id(key);
            p->loaded.push_back({key, klass, id});
            p->keys.push_back({id, id, klass, path + "/" + name});
        }
        ::closedir(stream);
    }
}

KeyStore::~KeyStore() = default;
const std::vector<KeyStore::Key> &KeyStore::keys() const { return p->keys; }

KeyStore::Verdict KeyStore::verify(int fd, const std::string &name) const
{
    Verdict verdict;
    auto attempt = [&](const Impl::Loaded *entry, int level, int flags, bool *ok) {
        const int duplicate = ::dup(fd);
        if (duplicate < 0 || ::lseek(duplicate, 0, SEEK_SET) < 0) { if (duplicate >= 0) ::close(duplicate); throw Error("cannot read " + name); }
        FD_t file = Fdopen(fdDup(duplicate), "r.ufdio");
        ::close(duplicate);
        if (!file || Ferror(file)) { if (file) Fclose(file); throw Error("cannot open " + name); }
        Ts transaction;
        rpmKeyring keyring = rpmKeyringNew();
        if (entry) rpmKeyringAddKey(keyring, entry->key);
        rpmtsSetKeyring(transaction.ts, keyring);
        rpmKeyringFree(keyring);
        rpmtsSetVfyLevel(transaction.ts, level);
        rpmtsSetVfyFlags(transaction.ts, flags);
        struct rpmQVKArguments_s qva;
        std::memset(&qva, 0, sizeof(qva));
        captured_log.clear();
        *ok = rpmVerifySignatures(&qva, transaction.ts, file, name.c_str()) == 0;
        Fclose(file);
    };
    std::string why;
    if (p->loaded.empty()) {
        bool ignored = false;
        attempt(nullptr, RPMSIG_VERIFIABLE_TYPE, RPMVSF_DEFAULT, &ignored);
        why = captured_log;
    }
    for (const Trust wanted : {Trust::System, Trust::Application}) {
        for (const auto &entry : p->loaded) {
            if (entry.klass != wanted) continue;
            bool ok = false;
            attempt(&entry, RPMSIG_VERIFIABLE_TYPE, RPMVSF_DEFAULT, &ok);
            if (ok) {
                verdict.trust = wanted;
                verdict.intact = true;
                verdict.signer_key_id = entry.id;
                return verdict;
            }
            if (why.empty()) why = captured_log;
        }
    }
    attempt(nullptr, RPMSIG_DIGEST_TYPE, RPMVSF_MASK_NOSIGNATURES, &verdict.intact);
    if (!verdict.intact) verdict.reason = "the package is damaged or was modified";
    else verdict.reason = why.find("NOKEY") != std::string::npos
                              ? "the package is signed by a publisher you have not trusted"
                              : "the package is not signed";
    return verdict;
}

std::string KeyStore_pin_path(const Config &config, const std::string &application_id)
{
    return config.path(config.state_dir + "/publishers/" + application_id + ".asc");
}

KeyStore::Verdict KeyStore::verify_package(int fd, const std::string &name, const std::string &application_id,
                                           const std::string &embedded_key) const
{
    Verdict verdict = verify(fd, name);
    if (verdict.trust != Trust::Untrusted || !verdict.intact || !valid_application_id(application_id)) return verdict;
    const std::string pin = KeyStore_pin_path(p->config, application_id);
    struct stat st;
    if (::lstat(pin.c_str(), &st) == 0) {
        if (!S_ISREG(st.st_mode) || st.st_uid != p->config.owner_uid || (st.st_mode & 022))
            throw Error("the pinned publisher key " + pin + " is not a root-owned regular file");
        rpmPubkey key = rpmPubkeyRead(pin.c_str());
        if (!key) throw Error("cannot parse the pinned publisher key " + pin);
        const bool ok = signature_matches(fd, name, key);
        if (ok) {
            verdict.trust = Trust::Application;
            verdict.pinned = true;
            verdict.signer_key_id = hex_key_id(key);
            verdict.publisher_name = key_user_id(key);
            verdict.publisher_fingerprint = hex_key_id(key);
            verdict.reason.clear();
        } else {
            verdict.publisher_mismatch = true;
            verdict.publisher_name = key_user_id(key);
            verdict.publisher_fingerprint = hex_key_id(key);
            verdict.reason = "the installed version came from " + (verdict.publisher_name.empty() ? "another publisher" : verdict.publisher_name)
                           + " (key " + verdict.publisher_fingerprint + "); this package is not signed by that key";
        }
        rpmPubkeyFree(key);
        return verdict;
    }
    if (embedded_key.empty()) return verdict;
    rpmPubkey key = parse_armored_key(embedded_key);
    if (!key) return verdict;
    if (signature_matches(fd, name, key)) {
        verdict.trust = Trust::Publisher;
        verdict.signer_key_id = hex_key_id(key);
        verdict.publisher_name = key_user_id(key);
        verdict.publisher_fingerprint = hex_key_id(key);
        verdict.publisher_key = embedded_key;
        verdict.reason = "first install from " + (verdict.publisher_name.empty() ? std::string("this publisher") : verdict.publisher_name)
                       + " (key " + verdict.publisher_fingerprint + ")";
    }
    rpmPubkeyFree(key);
    return verdict;
}

void KeyStore::pin_publisher(const std::string &application_id, const std::string &armored_key) const
{
    if (!valid_application_id(application_id)) throw Error("invalid application id " + application_id);
    rpmPubkey key = parse_armored_key(armored_key);
    if (!key) throw Error("the publisher key of " + application_id + " cannot be parsed");
    rpmPubkeyFree(key);
    const std::string directory = p->config.path(p->config.state_dir + "/publishers");
    ::mkdir(directory.c_str(), 0700);
    const std::string target = KeyStore_pin_path(p->config, application_id);
    const std::string temporary = target + ".new";
    ::unlink(temporary.c_str());
    const int out = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (out < 0) throw Error("cannot write " + temporary + ": " + util::errno_text(errno));
    const bool written = ::write(out, armored_key.data(), armored_key.size()) == ssize_t(armored_key.size()) && ::fsync(out) == 0;
    ::close(out);
    if (!written || ::rename(temporary.c_str(), target.c_str()) != 0) {
        ::unlink(temporary.c_str());
        throw Error("cannot pin the publisher of " + application_id);
    }
}

void KeyStore::unpin_publisher(const std::string &application_id) const
{
    if (valid_application_id(application_id)) ::unlink(KeyStore_pin_path(p->config, application_id).c_str());
}

namespace {
std::string string_tag(Header header, rpmTagVal tag)
{
    const char *value = headerGetString(header, tag);
    return value ? value : "";
}

std::vector<std::string> dependency_list(Header header, rpmTagVal name_tag)
{
    std::vector<std::string> result;
    rpmds dependencies = rpmdsNew(header, name_tag, 0);
    if (!dependencies) return result;
    rpmdsInit(dependencies);
    while (rpmdsNext(dependencies) >= 0) {
        std::string entry = rpmdsN(dependencies) ? rpmdsN(dependencies) : "";
        const char *evr = rpmdsEVR(dependencies);
        const rpmsenseFlags flags = rpmdsFlags(dependencies);
        if (evr && *evr) {
            std::string operation;
            if (flags & RPMSENSE_LESS) operation += '<';
            if (flags & RPMSENSE_GREATER) operation += '>';
            if (flags & RPMSENSE_EQUAL) operation += '=';
            entry += " " + (operation.empty() ? std::string("=") : operation) + " " + evr;
        }
        result.push_back(entry);
    }
    rpmdsFree(dependencies);
    return result;
}

struct ReadResult {
    Header header = nullptr;
    FD_t file = nullptr;
};

ReadResult open_package(int fd, const std::string &name)
{
    const int duplicate = ::dup(fd);
    if (duplicate < 0 || ::lseek(duplicate, 0, SEEK_SET) < 0) { if (duplicate >= 0) ::close(duplicate); throw Error("cannot read " + name); }
    ReadResult result;
    result.file = Fdopen(fdDup(duplicate), "r.ufdio");
    ::close(duplicate);
    if (!result.file || Ferror(result.file)) throw Error("cannot open " + name);
    Ts transaction;
    rpmtsSetVSFlags(transaction.ts, RPMVSF_MASK_NOSIGNATURES | RPMVSF_NOPAYLOAD);
    const rpmRC rc = rpmReadPackageFile(transaction.ts, result.file, name.c_str(), &result.header);
    if ((rc != RPMRC_OK && rc != RPMRC_NOKEY && rc != RPMRC_NOTTRUSTED) || !result.header) {
        Fclose(result.file);
        throw Error(name + " is not a valid RPM package");
    }
    return result;
}
}

std::string PackageInfo::nevra() const
{
    return name + "-" + (has_epoch ? std::to_string(epoch) + ":" : "") + version + "-" + release + "." + arch;
}

PackageInfo read_package(int fd, const std::string &name)
{
    ReadResult opened = open_package(fd, name);
    Header header = opened.header;
    PackageInfo info;
    info.name = string_tag(header, RPMTAG_NAME);
    info.version = string_tag(header, RPMTAG_VERSION);
    info.release = string_tag(header, RPMTAG_RELEASE);
    info.arch = string_tag(header, RPMTAG_ARCH);
    info.os = string_tag(header, RPMTAG_OS);
    info.summary = string_tag(header, RPMTAG_SUMMARY);
    info.license = string_tag(header, RPMTAG_LICENSE);
    info.source_rpm = string_tag(header, RPMTAG_SOURCERPM);
    info.payload_format = string_tag(header, RPMTAG_PAYLOADFORMAT);
    info.payload_compressor = string_tag(header, RPMTAG_PAYLOADCOMPRESSOR);
    if (headerIsEntry(header, RPMTAG_EPOCH)) {
        info.has_epoch = true;
        info.epoch = std::uint32_t(headerGetNumber(header, RPMTAG_EPOCH));
    }
    info.relocatable = headerIsEntry(header, RPMTAG_PREFIXES);
    info.installed_size = headerIsEntry(header, RPMTAG_LONGSIZE) ? headerGetNumber(header, RPMTAG_LONGSIZE)
                                                                  : headerGetNumber(header, RPMTAG_SIZE);
    info.provides = dependency_list(header, RPMTAG_PROVIDENAME);
    info.requires_ = dependency_list(header, RPMTAG_REQUIRENAME);
    info.conflicts = dependency_list(header, RPMTAG_CONFLICTNAME);
    info.obsoletes = dependency_list(header, RPMTAG_OBSOLETENAME);
    static const std::pair<rpmTagVal, const char *> script_tags[] = {
        {RPMTAG_PRETRANS, "pretrans"}, {RPMTAG_PRETRANSPROG, "pretrans"}, {RPMTAG_PREIN, "prein"},
        {RPMTAG_PREINPROG, "prein"}, {RPMTAG_POSTIN, "postin"}, {RPMTAG_POSTINPROG, "postin"},
        {RPMTAG_PREUN, "preun"}, {RPMTAG_PREUNPROG, "preun"}, {RPMTAG_POSTUN, "postun"},
        {RPMTAG_POSTUNPROG, "postun"}, {RPMTAG_POSTTRANS, "posttrans"}, {RPMTAG_POSTTRANSPROG, "posttrans"},
        {RPMTAG_VERIFYSCRIPT, "verifyscript"}, {RPMTAG_VERIFYSCRIPTPROG, "verifyscript"},
        {RPMTAG_TRIGGERSCRIPTS, "trigger"}, {RPMTAG_TRIGGERNAME, "trigger"},
        {RPMTAG_FILETRIGGERNAME, "filetrigger"}, {RPMTAG_FILETRIGGERSCRIPTS, "filetrigger"},
        {RPMTAG_TRANSFILETRIGGERNAME, "transfiletrigger"}, {RPMTAG_TRANSFILETRIGGERSCRIPTS, "transfiletrigger"},
        {RPMTAG_PREUNTRANS, "preuntrans"}, {RPMTAG_PREUNTRANSPROG, "preuntrans"},
        {RPMTAG_POSTUNTRANS, "postuntrans"}, {RPMTAG_POSTUNTRANSPROG, "postuntrans"},
        {RPMTAG_SYSUSERS, "sysusers"}};
    for (const auto &[tag, label] : script_tags)
        if (headerIsEntry(header, tag) && std::find(info.scripts.begin(), info.scripts.end(), label) == info.scripts.end())
            info.scripts.emplace_back(label);

    rpmfiles files = rpmfilesNew(nullptr, header, RPMTAG_BASENAMES, RPMFI_FLAGS_QUERY);
    if (files) {
        rpmfi iterator = rpmfilesIter(files, RPMFI_ITER_FWD);
        while (rpmfiNext(iterator) >= 0) {
            FileEntry entry;
            const char *path = rpmfiFN(iterator);
            entry.path = path ? path : "";
            entry.mode = rpmfiFMode(iterator);
            entry.user = rpmfiFUser(iterator) ? rpmfiFUser(iterator) : "";
            entry.group = rpmfiFGroup(iterator) ? rpmfiFGroup(iterator) : "";
            entry.link = rpmfiFLink(iterator) ? rpmfiFLink(iterator) : "";
            entry.caps = rpmfiFCaps(iterator) ? rpmfiFCaps(iterator) : "";
            entry.flags = rpmfiFFlags(iterator);
            entry.nlink = rpmfiFNlink(iterator);
            entry.size = rpmfiFSize(iterator);
            info.files.push_back(std::move(entry));
            if (info.files.size() > 10'000'000) break;
        }
        rpmfiFree(iterator);
        rpmfilesFree(files);
    }
    headerFree(header);
    Fclose(opened.file);
    return info;
}

std::optional<std::string> read_payload_file(int fd, const std::string &name, const std::string &wanted, std::size_t limit)
{
    ReadResult opened = open_package(fd, name);
    Header header = opened.header;
    const char *compressor = headerGetString(header, RPMTAG_PAYLOADCOMPRESSOR);
    const std::string flags = std::string("r.") + (compressor ? compressor : "gzip");
    FD_t payload = Fdopen(opened.file, flags.c_str());
    if (!payload || Ferror(payload)) { headerFree(header); throw Error("cannot open the payload of " + name); }
    rpmfiles files = rpmfilesNew(nullptr, header, RPMTAG_BASENAMES, RPMFI_FLAGS_QUERY);
    rpmfi iterator = rpmfiNewArchiveReader(payload, files, RPMFI_ITER_READ_ARCHIVE);
    std::optional<std::string> found;
    int rc;
    while ((rc = rpmfiNext(iterator)) >= 0) {
        const char *path = rpmfiFN(iterator);
        if (!path || wanted != path) continue;
        if (!S_ISREG(rpmfiFMode(iterator)) || !rpmfiArchiveHasContent(iterator)) break;
        std::string data;
        char buffer[4096];
        for (;;) {
            const auto count = rpmfiArchiveRead(iterator, buffer, sizeof(buffer));
            if (count < 0) { data.clear(); break; }
            if (count == 0) break;
            data.append(buffer, std::size_t(count));
            if (data.size() > limit) { data.clear(); break; }
        }
        found = std::move(data);
        break;
    }
    rpmfiArchiveClose(iterator);
    rpmfiFree(iterator);
    rpmfilesFree(files);
    headerFree(header);
    Fclose(payload);
    return found;
}

void classify(PackageInfo &info)
{
    info.klass.reset();
    info.application_id.clear();
    bool application = false, system = false;
    for (const auto &provide : info.provides) {
        const auto name = provide.substr(0, provide.find(' '));
        if (name == "openwave-application(" + info.name + ")") application = true;
        if (name == "openwave-system-package(" + info.name + ")") system = true;
    }
    if (application == system) return;
    info.klass = application ? PackageClass::Application : PackageClass::System;
    if (application) info.application_id = info.name;
}

}
