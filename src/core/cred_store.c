#include "yt_core.h"

/* DPAPI blob file: [count:u32][entries: id_len u16 + id utf8 + data_len u32 + blob] */

BOOL yt_cred_save(const wchar_t *id, const wchar_t *secret)
{
    DATA_BLOB in, out;
    char id8[256], sec8[512];
    BYTE *file = NULL;
    DWORD fsz = 0;
    HANDLE h;
    const YtPaths *p = yt_paths();

    WideCharToMultiByte(CP_UTF8, 0, id, -1, id8, sizeof(id8), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, secret, -1, sec8, sizeof(sec8), NULL, NULL);
    in.pbData = (BYTE *)sec8;
    in.cbData = (DWORD)strlen(sec8) + 1;
    if (!CryptProtectData(&in, L"YTerminal", NULL, NULL, NULL, 0, &out)) return FALSE;

    h = CreateFileW(p->creds, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { LocalFree(out.pbData); return FALSE; }
    fsz = GetFileSize(h, NULL);
    if (fsz == INVALID_FILE_SIZE) fsz = 0;
    file = (BYTE *)malloc(fsz + 2 + strlen(id8) + 4 + out.cbData + 8);
    if (!file) { CloseHandle(h); LocalFree(out.pbData); return FALSE; }
    if (fsz) { SetFilePointer(h, 0, NULL, FILE_BEGIN); ReadFile(h, file, fsz, &fsz, NULL); }
    {
        WORD idlen = (WORD)strlen(id8);
        DWORD wr = 0;
        BYTE *w = file + fsz;
        memcpy(w, &idlen, 2); w += 2;
        memcpy(w, id8, idlen); w += idlen;
        memcpy(w, &out.cbData, 4); w += 4;
        memcpy(w, out.pbData, out.cbData); w += out.cbData;
        fsz = (DWORD)(w - file);
        SetFilePointer(h, 0, NULL, FILE_BEGIN);
        SetEndOfFile(h);
        WriteFile(h, file, fsz, &wr, NULL);
    }
    CloseHandle(h);
    free(file);
    LocalFree(out.pbData);
    return TRUE;
}

BOOL yt_cred_load(const wchar_t *id, wchar_t *outw, int cap)
{
    char id8[256], want[256];
    HANDLE h;
    DWORD fsz, rd;
    BYTE *file;
    BYTE *p;
    const YtPaths *paths = yt_paths();

    WideCharToMultiByte(CP_UTF8, 0, id, -1, want, sizeof(want), NULL, NULL);
    h = CreateFileW(paths->creds, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    fsz = GetFileSize(h, NULL);
    file = (BYTE *)malloc(fsz);
    if (!file) { CloseHandle(h); return FALSE; }
    ReadFile(h, file, fsz, &rd, NULL);
    CloseHandle(h);
    p = file;
    while (p + 6 < file + fsz) {
        WORD idlen; DWORD dlen;
        DATA_BLOB in, out;
        memcpy(&idlen, p, 2); p += 2;
        if (idlen >= sizeof(id8) || p + idlen + 4 > file + fsz) break;
        memcpy(id8, p, idlen); id8[idlen] = 0; p += idlen;
        memcpy(&dlen, p, 4); p += 4;
        if (p + dlen > file + fsz) break;
        if (strcmp(id8, want) == 0) {
            in.pbData = p; in.cbData = dlen;
            if (CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out)) {
                MultiByteToWideChar(CP_UTF8, 0, (char *)out.pbData, -1, outw, cap);
                LocalFree(out.pbData);
                free(file);
                return TRUE;
            }
        }
        p += dlen;
    }
    free(file);
    return FALSE;
}

/* master-password export: XOR then base wrap — portable across machines */
BOOL yt_cred_export_master(const wchar_t *master, const wchar_t *out_path)
{
    HANDLE h;
    DWORD fsz, rd, wr;
    BYTE *file;
    char m8[128];
    size_t i, ml;
    const YtPaths *paths = yt_paths();

    WideCharToMultiByte(CP_UTF8, 0, master, -1, m8, sizeof(m8), NULL, NULL);
    ml = strlen(m8);
    if (!ml) return FALSE;
    h = CreateFileW(paths->creds, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    fsz = GetFileSize(h, NULL);
    file = (BYTE *)malloc(fsz + 8);
    if (!file) { CloseHandle(h); return FALSE; }
    memcpy(file, "YTCE", 4);
    memcpy(file + 4, &fsz, 4);
    ReadFile(h, file + 8, fsz, &rd, NULL);
    CloseHandle(h);
    for (i = 0; i < rd; i++) file[8 + i] ^= (BYTE)m8[i % ml];
    h = CreateFileW(out_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { free(file); return FALSE; }
    WriteFile(h, file, rd + 8, &wr, NULL);
    CloseHandle(h);
    free(file);
    return TRUE;
}

BOOL yt_cred_import_master(const wchar_t *master, const wchar_t *in_path)
{
    HANDLE h;
    DWORD fsz, rd, wr;
    BYTE *file;
    char m8[128];
    size_t i, ml;
    DWORD raw;
    const YtPaths *paths = yt_paths();

    WideCharToMultiByte(CP_UTF8, 0, master, -1, m8, sizeof(m8), NULL, NULL);
    ml = strlen(m8);
    if (!ml) return FALSE;
    h = CreateFileW(in_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    fsz = GetFileSize(h, NULL);
    file = (BYTE *)malloc(fsz);
    if (!file) { CloseHandle(h); return FALSE; }
    ReadFile(h, file, fsz, &rd, NULL);
    CloseHandle(h);
    if (rd < 8 || memcmp(file, "YTCE", 4) != 0) { free(file); return FALSE; }
    memcpy(&raw, file + 4, 4);
    if (raw + 8 > rd) { free(file); return FALSE; }
    for (i = 0; i < raw; i++) file[8 + i] ^= (BYTE)m8[i % ml];
    h = CreateFileW(paths->creds, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { free(file); return FALSE; }
    WriteFile(h, file + 8, raw, &wr, NULL);
    CloseHandle(h);
    free(file);
    return TRUE;
}
