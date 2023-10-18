#ifndef _CRYPT_H
#define _CRYPT_H

#ifdef __APPLE__
#define hidden __attribute__((__visibility__("hidden")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct crypt_data {
	int initialized;
	char __buf[256];
};

char *crypt(const char *, const char *);
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
