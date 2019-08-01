#include "infra.h"

#if PLATFORM_WINDOWS

bool try_get_windows_user_sid(const std::string& user_name, /*out*/std::string& result)
{
    char buffer[256];
    DWORD size = (DWORD)std::size(buffer);

    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD sid_size = SECURITY_MAX_SID_SIZE;
    SID_NAME_USE snu = SidTypeInvalid;
    size = sizeof(buffer);
    BOOL success = LookupAccountNameA(
        NULL,  //use this system
        user_name.c_str(),  //the user to look up
        sid,  //the returned SID
        &sid_size,  //the size of the SID returned
        buffer,  //the returned domain name
        &size,  //the size of the domain name
        &snu);  //the type of sid
    if (!success) {
        const DWORD gle = GetLastError();
        LOG_WARN("LookupAccountNameA() for {} failed. GetLastError = {} ({})", user_name, gle, infra::str_getlasterror(gle));
        result = "";
        return false;
    }

    LPSTR str_sid = NULL;
    success = ConvertSidToStringSidA(sid, &str_sid);
    if (!success) {
        const DWORD gle = GetLastError();
        LOG_WARN("ConvertSidToStringSidA() for {} failed. GetLastError = {} ({})", user_name, gle, infra::str_getlasterror(gle));
        result = "";
        return false;
    }

    result = str_sid;
    (void)LocalFree(str_sid);
    LOG_TRACE("User {} SID by LookupAccountNameA: {}", user_name, result);
    return true;
}

bool try_get_home_by_sid(const std::string& sid, /*out*/std::string& home)
{
    static constexpr const DWORD BUF_SIZE = 2048;  // should be more than enough

    home.resize(BUF_SIZE);
    infra::sweeper error_cleanup = [&]() {
        home.clear();
    };

    HKEY hKey;
    LSTATUS status = RegOpenKeyExA(
        HKEY_LOCAL_MACHINE,
        ("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\" + sid).c_str(),
        0,
        KEY_READ,
        &hKey);
    if (status != ERROR_SUCCESS) {
        LOG_WARN("RegOpenKeyExA() HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\{} "
                 "failed with {} ({})", sid, status, infra::str_getlasterror(status));
        return false;
    }
    const infra::sweeper sweep_hkey = [&]() {
        (void)RegCloseKey(hKey);
    };

    DWORD len = BUF_SIZE;
    DWORD valueType;
    status = RegQueryValueExA(
        hKey,
        "ProfileImagePath",
        NULL,
        &valueType,
        (BYTE*)home.data(),
        &len);
    if (status != ERROR_SUCCESS) {
        LOG_WARN("RegQueryValueExA() HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\{}\\ProfileImagePath "
                 "failed with {} ({})", sid, status, infra::str_getlasterror(status));
        return false;
    }

    if (valueType == REG_EXPAND_SZ) {
        home.resize(len - 1);  // exclude tail '\0'

        std::string buffer;
        buffer.resize(BUF_SIZE);
        if (!ExpandEnvironmentStringsForUserA(NULL, home.c_str(), buffer.data(), BUF_SIZE)) {
            const int gle = GetLastError();
            LOG_ERROR("ExpandEnvironmentStringsForUserA() failed. GetLastError = {} ({})", gle, infra::str_getlasterror(gle));
            return false;
        }

        buffer.resize(strlen(buffer.data()));  // trim ending chars
        home = std::move(buffer);
    }
    else if (valueType == REG_SZ) {
        home.resize(len - 1);  // exclude tail '\0'
    }
    else {
        LOG_WARN("RegQueryValueExA() HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\{}\\ProfileImagePath "
                 "unexpected value type. Expect REG_EXPAND_SZ or REG_SZ, but got {}", sid, valueType);
        return false;
    }

    LOG_TRACE("Query home directory from registry for SID {} got: \"{}\"", sid, home);

    error_cleanup.suppress_sweep();
    return true;
}

#elif PLATFORM_LINUX

bool try_get_home_by_name(const std::string& name, /*out*/std::string& home)
{
    size_t bufsize = (size_t)sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufsize == (size_t)-1)          /* Value was indeterminate */
        bufsize = 16384;        /* Should be more than enough */

    char* const buf = (char*)malloc(bufsize);
    if (buf == NULL) {
        LOG_ERROR("Can't malloc() {} bytes for buffer", bufsize);
        return false;
    }
    const infra::sweeper sweep_free = [&]() {
        free(buf);
    };

    struct passwd pwd { };
    struct passwd* result = nullptr;
    const int err = getpwnam_r(name.c_str(), &pwd, buf, bufsize, &result);
    if (result == NULL) {
        if (err == 0) {
            LOG_WARN("User not found by getpwnam_r(): {}", name);
        }
        else {
            LOG_WARN("getpwnam_r() failed with {} ({})", err, strerror(err));
        }
        return false;
    }

    home = pwd.pw_dir;
    LOG_TRACE("Query home directory from getpwnam_r() for {} got: {}", name, home);

    return true;
}

#else
#   error "Unknown platform"
#endif



bool infra::get_user_name(/*out*/ user_name_t& name)
{
#if PLATFORM_WINDOWS
    char buffer[256];

    // Get user name
    DWORD size = sizeof(buffer);
    if (!GetUserNameA(buffer, &size)) {
        const DWORD gle = GetLastError();
        LOG_WARN("GetUserNameA() failed. GetLastError = {} ({})", gle, str_getlasterror(gle));
        return false;
    }
    else {
        name.user_name = buffer;
        LOG_TRACE("Current user name by GetUserNameA: {}", name.user_name);
    }

    // Get domain\user name
    size = sizeof(buffer);
    if (!GetUserNameExA(EXTENDED_NAME_FORMAT::NameSamCompatible, buffer, &size)) {
        const DWORD gle = GetLastError();
        LOG_WARN("GetUserNameExA(NameSamCompatible) failed. GetLastError = {} ({})", gle, str_getlasterror(gle));
        name.domain_user_name = "";
    }
    else {
        name.domain_user_name = buffer;
        LOG_TRACE("Current domain\\user name by GetUserNameExA: {}", name.domain_user_name);
    }

    // Get user principal name
    size = sizeof(buffer);
    if (!GetUserNameExA(EXTENDED_NAME_FORMAT::NameUserPrincipal, buffer, &size)) {
        const DWORD gle = GetLastError();
        // If we didn't join a domain, or UPN is not enabled, GetUserNameExA is expected to fail
        // Thus, we do not emit a LOG_WARN
        LOG_DEBUG("GetUserNameExA(NameUserPrincipal) failed. GetLastError = {} ({})", gle, str_getlasterror(gle));
        name.user_principal_name = "";
    }
    else {
        name.user_principal_name = buffer;
        LOG_TRACE("Current user principal name name by GetUserNameExA: {}", name.user_principal_name);
    }

    // Get user SID
    (void)try_get_windows_user_sid(name.domain_user_name, /*out*/ name.user_sid);

    return true;

#elif PLATFORM_LINUX

    char buffer[256];

    const int err = getlogin_r(buffer, std::size(buffer));
    if (err != 0) {
        LOG_WARN("getpwnam_r() failed with {} ({})", err, strerror(err));
        return false;
    }
    else {
        name.user_name = buffer;
        LOG_TRACE("Current user name by getpwnam_r: {}", name.user_name);
    }

    name.domain_user_name = "";
    name.user_sid = "";

    return true;

#else
#   error "Unknown platform"
#endif
}



stdfs::path infra::get_user_home_path(const user_name_t& name)
{
#if PLATFORM_WINDOWS
    std::string home;
    if (!name.user_sid.empty()) {
        if (try_get_home_by_sid(name.user_sid, /*out*/home)) {
            LOG_TRACE("Got home from SID {}: {}", name.user_sid, home);
            return home;
        }
    }
    if (!name.domain_user_name.empty()) {
        std::string sid;
        if (try_get_windows_user_sid(name.domain_user_name, /*out*/sid)) {
            if (try_get_home_by_sid(sid, home)) {
                LOG_TRACE("Got home from domain\\user {}: {}", name.domain_user_name, home);
                return home;
            }
        }
    }
    if (!name.user_principal_name.empty()) {
        std::string sid;
        if (try_get_windows_user_sid(name.user_principal_name, /*out*/sid)) {
            if (try_get_home_by_sid(sid, home)) {
                LOG_TRACE("Got home from user principal name {}: {}", name.user_principal_name, home);
                return home;
            }
        }
    }
    if (!name.user_name.empty()) {
        std::string sid;
        if (try_get_windows_user_sid(name.user_name, /*out*/sid)) {
            if (try_get_home_by_sid(sid, home)) {
                LOG_TRACE("Got home from user {}: {}", name.user_name, home);
                return home;
            }
        }
    }

    LOG_WARN("Can't get home path for user (user_sid={},domain_user_name={},user_name={})",
             name.user_sid, name.domain_user_name, name.user_name);
    return stdfs::path();


#elif PLATFORM_LINUX

    std::string home;
    if (!name.user_name.empty()) {
        if (try_get_home_by_name(name.user_name, home)) {
            LOG_TRACE("Got home from user {}: {}", name.user_name, home);
            return home;
        }
    }
    if (!name.domain_user_name.empty()) {
        if (try_get_home_by_name(name.domain_user_name, home)) {
            LOG_TRACE("Got home from domain\\user {}: {}", name.domain_user_name, home);
            return home;
        }
    }
    if (!name.user_principal_name.empty()) {
        if (try_get_home_by_name(name.user_principal_name, home)) {
            LOG_TRACE("Got home from user principal name {}: {}", name.user_principal_name, home);
            return home;
        }
    }
    if (!name.user_sid.empty()) {
        if (try_get_home_by_name(name.user_sid, home)) {
            LOG_TRACE("Got home from SID {}: {}", name.user_sid, home);
            return home;
        }
    }

    LOG_WARN("Can't get home path for user: {}", name.to_string());
    return stdfs::path();

#else
#   error "Unknown platform"
#endif
}
