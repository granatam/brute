#ifndef _CRYPT_H
#define _CRYPT_H

#define hidden __attribute__((__visibility__("hidden")))

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __FreeBSD__
struct crypt_data {
	int initialized;
	char __buf[256];
};

char *crypt(const char *, const char *);
#else
#define _XOPEN_SOURCE
#include <unistd.h>
#endif

char *crypt_r(const char *, const char *, struct crypt_data *);

hidden char *__crypt_r(const char *, const char *, struct crypt_data *);
hidden char *__crypt_des(const char *, const char *, char *);
hidden char *__crypt_md5(const char *, const char *, char *);
hidden char *__crypt_blowfish(const char *, const char *, char *);
hidden char *__crypt_sha256(const char *, const char *, char *);
hidden char *__crypt_sha512(const char *, const char *, char *);

#ifdef __cplusplus
}
#endif


#endif
