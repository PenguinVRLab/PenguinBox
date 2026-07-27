/*
 * QEMU Guest Agent win32-specific command implementations for SSH keys.
 * The implementation is opinionated and expects the SSH implementation to
 * be OpenSSH.
 *
 * Copyright Schweitzer Engineering Laboratories. 2024
 *
 * Authors:
 *  Aidan Leuck <aidan_leuck@selinc.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <aclapi.h>
#include <qga-qapi-types.h>

#include "commands-common-ssh.h"
#include "commands-windows-ssh.h"
#include "guest-agent-core.h"
#include "limits.h"
#include "lmaccess.h"
#include "lmapibuf.h"
#include "lmerr.h"
#include "qapi/error.h"

#include "qga-qapi-commands.h"
#include "sddl.h"
#include "shlobj.h"
#include "userenv.h"

#define AUTHORIZED_KEY_FILE "authorized_keys"
#define AUTHORIZED_KEY_FILE_ADMIN "administrators_authorized_keys"
#define LOCAL_SYSTEM_SID "S-1-5-18"
#define ADMIN_SID "S-1-5-32-544"

void free_userInfo(PWindowsUserInfo info)
{
    g_free(info->sshDirectory);
    g_free(info->authorizedKeyFile);
    LocalFree(info->SSID);
    g_free(info->username);
    g_free(info);
}

static char *get_admin_ssh_folder(Error **errp)
{
    g_autofree char *programDataPath = NULL;
    char *authkeys_path = NULL;
    PWSTR pgDataW = NULL;
    g_autoptr(GError) gerr = NULL;

    HRESULT folderResult =
        SHGetKnownFolderPath(&FOLDERID_ProgramData, 0, NULL, &pgDataW);
    if (folderResult != S_OK) {
        error_setg(errp, "Failed to retrieve ProgramData folder");
        return NULL;
    }

    programDataPath = g_utf16_to_utf8(pgDataW, -1, NULL, NULL, &gerr);
    CoTaskMemFree(pgDataW);
    if (!programDataPath) {
        error_setg(errp,
                   "Failed converting ProgramData folder path to UTF-16 %s",
                   gerr->message);
        return NULL;
    }

    authkeys_path = g_build_filename(programDataPath, "ssh", NULL);
    return authkeys_path;
}

static char *get_ssh_folder(const char *username, const bool isAdmin,
                            Error **errp)
{
    DWORD maxSize = MAX_PATH;
    g_autofree char *profilesDir = g_new0(char, maxSize);

    if (isAdmin) {
        return get_admin_ssh_folder(errp);
    }

    BOOL ret = GetProfilesDirectory(profilesDir, &maxSize);
    if (!ret) {
        error_setg_win32(errp, GetLastError(),
                         "failed to retrieve profiles directory");
        return NULL;
    }

    return g_build_filename(profilesDir, username, ".ssh", NULL);
}

static bool create_acl_user(PWindowsUserInfo userInfo, PACL *pACL, Error **errp)
{
    const int aclSize = 1;
    PACL newACL = NULL;
    EXPLICIT_ACCESS eAccess[1];
    PSID userPSID = NULL;

    bool converted = ConvertStringSidToSid(userInfo->SSID, &userPSID);
    if (!converted) {
        error_setg_win32(errp, GetLastError(), "failed to retrieve user %s SID",
                         userInfo->username);
        goto error;
    }

    eAccess[0].grfAccessPermissions = GENERIC_ALL;
    eAccess[0].grfAccessMode = SET_ACCESS;
    eAccess[0].grfInheritance = NO_INHERITANCE;
    eAccess[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    eAccess[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    eAccess[0].Trustee.ptstrName = (LPTSTR)userPSID;

    DWORD setResult;

    if (*pACL) {
        setResult = SetEntriesInAcl(aclSize, eAccess, *pACL, &newACL);
    } else {
        setResult = SetEntriesInAcl(aclSize, eAccess, NULL, &newACL);
    }

    if (setResult != ERROR_SUCCESS) {
        error_setg_win32(errp, GetLastError(),
                         "failed to set ACL entries for user %s %lu",
                         userInfo->username, setResult);
        goto error;
    }

    LocalFree(*pACL);
    *pACL = newACL;

    LocalFree(userPSID);
    return true;
error:
    LocalFree(userPSID);
    return false;
}

static bool create_acl_base(PACL *pACL, Error **errp)
{
    PSID adminGroupPSID = NULL;
    PSID systemPSID = NULL;

    const int aclSize = 2;
    EXPLICIT_ACCESS eAccess[2];

    const char *systemSID = LOCAL_SYSTEM_SID;
    bool converted = ConvertStringSidToSid(systemSID, &systemPSID);
    if (!converted) {
        error_setg_win32(errp, GetLastError(), "failed to retrieve system SID");
        goto error;
    }

    eAccess[0].grfAccessPermissions = GENERIC_ALL;
    eAccess[0].grfAccessMode = SET_ACCESS;
    eAccess[0].grfInheritance = NO_INHERITANCE;
    eAccess[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    eAccess[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    eAccess[0].Trustee.ptstrName = (LPTSTR)systemPSID;

    const char *adminSID = ADMIN_SID;
    converted = ConvertStringSidToSid(adminSID, &adminGroupPSID);
    if (!converted) {
        error_setg_win32(errp, GetLastError(), "failed to retrieve Admin SID");
        goto error;
    }

    eAccess[1].grfAccessPermissions = GENERIC_ALL;
    eAccess[1].grfAccessMode = SET_ACCESS;
    eAccess[1].grfInheritance = NO_INHERITANCE;
    eAccess[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    eAccess[1].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    eAccess[1].Trustee.ptstrName = (LPTSTR)adminGroupPSID;

    PACL pNewACL = NULL;
    DWORD setResult;

    if (*pACL) {
        setResult = SetEntriesInAcl(aclSize, eAccess, *pACL, &pNewACL);
    } else {
        setResult = SetEntriesInAcl(aclSize, eAccess, NULL, &pNewACL);
    }

    if (setResult != ERROR_SUCCESS) {
        error_setg_win32(errp, GetLastError(),
                         "failed to set base ACL entries for system user and "
                         "admin group %lu",
                         setResult);
        goto error;
    }

    LocalFree(adminGroupPSID);
    LocalFree(systemPSID);

    LocalFree(*pACL);

    *pACL = pNewACL;

    return true;

error:
    LocalFree(adminGroupPSID);
    LocalFree(systemPSID);
    return false;
}

static bool create_acl(PWindowsUserInfo userInfo, PACL *pACL, Error **errp)
{
    if (!create_acl_base(pACL, errp)) {
        return false;
    }

    if (!userInfo->isAdmin) {
        if (!create_acl_user(userInfo, pACL, errp)) {
            return false;
        }

        return true;
    }

    return true;
}
static bool create_ssh_directory(WindowsUserInfo *userInfo, Error **errp)
{
    PACL pNewACL = NULL;
    g_autofree PSECURITY_DESCRIPTOR pSD = NULL;

    if (!create_acl(userInfo, &pNewACL, errp)) {
        goto error;
    }

    pSD = g_malloc(SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION)) {
        error_setg_win32(errp, GetLastError(),
                         "Failed to initialize security descriptor");
        goto error;
    }

    if (!SetSecurityDescriptorDacl(pSD, TRUE, pNewACL, FALSE)) {
        error_setg_win32(errp, GetLastError(),
                         "Failed to set security descriptor ACL");
        goto error;
    }

    SECURITY_ATTRIBUTES sAttr;
    sAttr.bInheritHandle = FALSE;
    sAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    sAttr.lpSecurityDescriptor = pSD;

    BOOL created = CreateDirectory(userInfo->sshDirectory, &sAttr);
    if (!created) {
        error_setg_win32(errp, GetLastError(), "failed to create directory %s",
                         userInfo->sshDirectory);
        goto error;
    }

    LocalFree(pNewACL);
    return true;
error:
    LocalFree(pNewACL);
    return false;
}

static bool set_file_permissions(PWindowsUserInfo userInfo, Error **errp)
{
    PACL pACL = NULL;
    PSID userPSID = NULL;

    if (!create_acl(userInfo, &pACL, errp)) {
        goto error;
    }

    bool converted = ConvertStringSidToSid(userInfo->SSID, &userPSID);
    if (!converted) {
        error_setg_win32(errp, GetLastError(), "failed to retrieve user %s SID",
                         userInfo->username);
        goto error;
    }

    const SE_OBJECT_TYPE securityBitFlags =
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION;

    if (SetNamedSecurityInfo(userInfo->authorizedKeyFile, SE_FILE_OBJECT,
                             securityBitFlags, userPSID, NULL, pACL,
                             NULL) != ERROR_SUCCESS) {
        error_setg_win32(errp, GetLastError(),
                         "failed to set file security for file %s",
                         userInfo->authorizedKeyFile);
        goto error;
    }

    LocalFree(pACL);
    LocalFree(userPSID);
    return true;

error:
    LocalFree(pACL);
    LocalFree(userPSID);

    return false;
}

static bool write_authkeys(WindowsUserInfo *userInfo, GStrv authkeys,
                           Error **errp)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) err = NULL;

    contents = g_strjoinv("\n", authkeys);

    if (!g_file_set_contents(userInfo->authorizedKeyFile, contents, -1, &err)) {
        error_setg(errp, "failed to write to '%s': %s",
                   userInfo->authorizedKeyFile, err->message);
        return false;
    }

    if (!set_file_permissions(userInfo, errp)) {
        return false;
    }

    return true;
}

static bool get_user_info(PWindowsUserInfo *userInfo, const char *username,
                          Error **errp)
{
    DWORD infoLevel = 4;
    LPUSER_INFO_4 uBuf = NULL;
    g_autofree wchar_t *wideUserName = NULL;
    g_autoptr(GError) gerr = NULL;
    PSID psid = NULL;

    wideUserName = g_utf8_to_utf16(username, -1, NULL, NULL, &gerr);
    if (!wideUserName) {
        goto error;
    }

    PWindowsUserInfo uData = g_new0(WindowsUserInfo, 1);

    *userInfo = uData;

    NET_API_STATUS result =
        NetUserGetInfo(NULL, wideUserName, infoLevel, (LPBYTE *)&uBuf);
    if (result != NERR_Success) {
        if (result == NERR_UserNotFound) {
            error_setg(errp, "User %s was not found", username);
            goto error;
        }

        error_setg(errp,
                   "Received unexpected error when asking for user info: Error "
                   "Code %lu",
                   result);
        goto error;
    }

    uData->username = g_strdup(username);
    uData->isAdmin = uBuf->usri4_priv == USER_PRIV_ADMIN;
    psid = uBuf->usri4_user_sid;

    char *sidStr = NULL;

    if (!ConvertSidToStringSid(psid, &sidStr)) {
        error_setg_win32(errp, GetLastError(),
                         "failed to get SID string for user %s", username);
        goto error;
    }

    uData->SSID = sidStr;

    char *sshFolder = get_ssh_folder(username, uData->isAdmin, errp);
    if (sshFolder == NULL) {
        goto error;
    }

    const char *authorizedKeyFile =
        uData->isAdmin ? AUTHORIZED_KEY_FILE_ADMIN : AUTHORIZED_KEY_FILE;
    char *authorizedKeyPath =
        g_build_filename(sshFolder, authorizedKeyFile, NULL);
    uData->sshDirectory = sshFolder;
    uData->authorizedKeyFile = authorizedKeyPath;

    NetApiBufferFree(uBuf);
    return true;
error:
    if (uBuf) {
        NetApiBufferFree(uBuf);
    }

    return false;
}

GuestAuthorizedKeys *qmp_guest_ssh_get_authorized_keys(const char *username,
                                                       Error **errp)
{
    GuestAuthorizedKeys *keys = NULL;
    g_auto(GStrv) authKeys = NULL;
    g_autoptr(GuestAuthorizedKeys) ret = NULL;
    g_auto(PWindowsUserInfo) userInfo = NULL;

    if (!get_user_info(&userInfo, username, errp)) {
        return NULL;
    }

    authKeys = read_authkeys(userInfo->authorizedKeyFile, errp);
    if (authKeys == NULL) {
        return NULL;
    }

    ret = g_new0(GuestAuthorizedKeys, 1);
    for (int i = 0; authKeys[i] != NULL; i++) {
        g_strstrip(authKeys[i]);
        if (!authKeys[i][0] || authKeys[i][0] == '#') {
            continue;
        }

        QAPI_LIST_PREPEND(ret->keys, g_strdup(authKeys[i]));
    }

    keys = g_steal_pointer(&ret);
    return keys;
}

void qmp_guest_ssh_add_authorized_keys(const char *username, strList *keys,
                                       bool has_reset, bool reset, Error **errp)
{
    g_auto(PWindowsUserInfo) userInfo = NULL;
    g_auto(GStrv) authkeys = NULL;
    strList *k;
    size_t nkeys, nauthkeys;

    if (!check_openssh_pub_keys(keys, &nkeys, errp)) {
        return;
    }

    if (!get_user_info(&userInfo, username, errp)) {
        return;
    }

    reset = has_reset && reset;
    if (!reset) {
        authkeys = read_authkeys(userInfo->authorizedKeyFile, NULL);
    }

    if (!g_file_test(userInfo->sshDirectory, G_FILE_TEST_IS_DIR)) {
        BOOL success = create_ssh_directory(userInfo, errp);
        if (!success) {
            return;
        }
    }

    nauthkeys = authkeys ? g_strv_length(authkeys) : 0;
    authkeys = g_realloc_n(authkeys, nauthkeys + nkeys + 1, sizeof(char *));

    memset(authkeys + nauthkeys, 0, (nkeys + 1) * sizeof(char *));

    for (k = keys; k != NULL; k = k->next) {
        if (g_strv_contains((const gchar *const *)authkeys, k->value)) {
            continue;
        }

        authkeys[nauthkeys++] = g_strdup(k->value);
    }

    write_authkeys(userInfo, authkeys, errp);
}

void qmp_guest_ssh_remove_authorized_keys(const char *username, strList *keys,
                                          Error **errp)
{
    g_auto(PWindowsUserInfo) userInfo = NULL;
    g_autofree struct passwd *p = NULL;
    g_autofree GStrv new_keys = NULL;
    g_auto(GStrv) authkeys = NULL;
    GStrv a;
    size_t nkeys = 0;

    if (!check_openssh_pub_keys(keys, NULL, errp)) {
        return;
    }

    if (!get_user_info(&userInfo, username, errp)) {
        return;
    }

    authkeys = read_authkeys(userInfo->authorizedKeyFile, errp);
    if (authkeys == NULL) {
        return;
    }

    new_keys = g_new0(char *, g_strv_length(authkeys) + 1);
    for (a = authkeys; *a != NULL; a++) {
        strList *k;

        for (k = keys; k != NULL; k = k->next) {
            if (g_str_equal(k->value, *a)) {
                break;
            }
        }

        if (k != NULL) {
            continue;
        }

        new_keys[nkeys++] = *a;
    }

    write_authkeys(userInfo, new_keys, errp);
}
