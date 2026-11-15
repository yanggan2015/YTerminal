#include "yt_core.h"
#include <libssh2.h>
#include <libssh2_sftp.h>

extern LIBSSH2_SESSION *yt_ssh_libsession(YtSession *s);

typedef struct {
    LIBSSH2_SFTP *sftp;
    YtSession *owner;
} SftpImpl;

void *yt_sftp_open(YtSession *s)
{
    LIBSSH2_SESSION *sess;
    SftpImpl *impl;
    LIBSSH2_SFTP *sftp;
    if (!s) return NULL;
    EnterCriticalSection(&s->lock);
    sess = yt_ssh_libsession(s);
    if (!sess) { LeaveCriticalSection(&s->lock); return NULL; }
    /* 短时阻塞初始化；必须与 reader 互斥 */
    libssh2_session_set_blocking(sess, 1);
    sftp = libssh2_sftp_init(sess);
    libssh2_session_set_blocking(sess, 0);
    LeaveCriticalSection(&s->lock);
    if (!sftp) return NULL;
    impl = (SftpImpl *)calloc(1, sizeof(SftpImpl));
    impl->sftp = sftp;
    impl->owner = s;
    return impl;
}

void yt_sftp_close(void *sftp)
{
    SftpImpl *impl = (SftpImpl *)sftp;
    YtSession *s;
    if (!impl) return;
    s = impl->owner;
    if (s) EnterCriticalSection(&s->lock);
    if (impl->sftp) {
        libssh2_sftp_shutdown(impl->sftp);
        impl->sftp = NULL;
    }
    if (s) LeaveCriticalSection(&s->lock);
    free(impl);
}

int yt_sftp_listdir(void *sftp, const char *path, char *out, int cap)
{
    SftpImpl *impl = (SftpImpl *)sftp;
    LIBSSH2_SFTP_HANDLE *dir;
    LIBSSH2_SESSION *sess;
    char mem[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int used = 0;
    if (!impl || !impl->sftp || !impl->owner || !out || cap < 2) return -1;
    EnterCriticalSection(&impl->owner->lock);
    sess = yt_ssh_libsession(impl->owner);
    if (!sess) { LeaveCriticalSection(&impl->owner->lock); return -1; }
    libssh2_session_set_blocking(sess, 1);
    dir = libssh2_sftp_opendir(impl->sftp, path ? path : ".");
    if (!dir) {
        libssh2_session_set_blocking(sess, 0);
        LeaveCriticalSection(&impl->owner->lock);
        return -1;
    }
    out[0] = 0;
    while (libssh2_sftp_readdir(dir, mem, sizeof(mem), &attrs) > 0) {
        int n = snprintf(out + used, (size_t)(cap - used), "%c\t%s\n",
                         LIBSSH2_SFTP_S_ISDIR(attrs.permissions) ? 'D' : 'F', mem);
        if (n < 0 || used + n >= cap) break;
        used += n;
    }
    libssh2_sftp_closedir(dir);
    libssh2_session_set_blocking(sess, 0);
    LeaveCriticalSection(&impl->owner->lock);
    return used;
}

int yt_sftp_download(void *sftp, const char *remote, const wchar_t *local)
{
    SftpImpl *impl = (SftpImpl *)sftp;
    LIBSSH2_SFTP_HANDLE *h;
    LIBSSH2_SESSION *sess;
    FILE *fp;
    char buf[8192];
    char path[YT_PATH_MAX];
    ssize_t n;
    if (!impl || !impl->owner) return -1;
    WideCharToMultiByte(CP_UTF8, 0, local, -1, path, sizeof(path), NULL, NULL);
    EnterCriticalSection(&impl->owner->lock);
    sess = yt_ssh_libsession(impl->owner);
    if (!sess) { LeaveCriticalSection(&impl->owner->lock); return -1; }
    libssh2_session_set_blocking(sess, 1);
    h = libssh2_sftp_open(impl->sftp, remote, LIBSSH2_FXF_READ, 0);
    if (!h) {
        libssh2_session_set_blocking(sess, 0);
        LeaveCriticalSection(&impl->owner->lock);
        return -1;
    }
    fp = fopen(path, "wb");
    if (!fp) {
        libssh2_sftp_close(h);
        libssh2_session_set_blocking(sess, 0);
        LeaveCriticalSection(&impl->owner->lock);
        return -1;
    }
    while ((n = libssh2_sftp_read(h, buf, sizeof(buf))) > 0) fwrite(buf, 1, (size_t)n, fp);
    fclose(fp);
    libssh2_sftp_close(h);
    libssh2_session_set_blocking(sess, 0);
    LeaveCriticalSection(&impl->owner->lock);
    return 0;
}

int yt_sftp_upload(void *sftp, const wchar_t *local, const char *remote)
{
    SftpImpl *impl = (SftpImpl *)sftp;
    LIBSSH2_SFTP_HANDLE *h;
    LIBSSH2_SESSION *sess;
    FILE *fp;
    char buf[8192];
    char path[YT_PATH_MAX];
    size_t n;
    if (!impl || !impl->owner) return -1;
    WideCharToMultiByte(CP_UTF8, 0, local, -1, path, sizeof(path), NULL, NULL);
    fp = fopen(path, "rb");
    if (!fp) return -1;
    EnterCriticalSection(&impl->owner->lock);
    sess = yt_ssh_libsession(impl->owner);
    if (!sess) { fclose(fp); LeaveCriticalSection(&impl->owner->lock); return -1; }
    libssh2_session_set_blocking(sess, 1);
    h = libssh2_sftp_open(impl->sftp, remote,
                          LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                          LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    if (!h) {
        fclose(fp);
        libssh2_session_set_blocking(sess, 0);
        LeaveCriticalSection(&impl->owner->lock);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        char *p = buf; size_t left = n;
        while (left) {
            ssize_t rc = libssh2_sftp_write(h, p, left);
            if (rc < 0) break;
            p += rc; left -= (size_t)rc;
        }
    }
    fclose(fp);
    libssh2_sftp_close(h);
    libssh2_session_set_blocking(sess, 0);
    LeaveCriticalSection(&impl->owner->lock);
    return 0;
}
