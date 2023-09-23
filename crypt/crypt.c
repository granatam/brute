#include "crypt.h"

#include <stdlib.h>
#include <errno.h>

char *
__crypt_r (const char *key, const char *salt,
	   struct crypt_data * __restrict data)
{
  ufc_long res[4];
  char ktab[9];
  ufc_long xx = 25; /* to cope with GCC long long compiler bugs */

#ifdef _LIBC
  /* Try to find out whether we have to use MD5 encryption replacement.  */
  if (strncmp (md5_salt_prefix, salt, sizeof (md5_salt_prefix) - 1) == 0)
    {
  //     /* FIPS rules out MD5 password encryption.  */
  //     if (fips_enabled_p ())
	// {
	//   __set_errno (EPERM);
	//   return NULL;
	// }
      return __md5_crypt_r (key, salt, (char *) data,
			    sizeof (struct crypt_data));
    }

  /* Try to find out whether we have to use SHA256 encryption replacement.  */
  if (strncmp (sha256_salt_prefix, salt, sizeof (sha256_salt_prefix) - 1) == 0)
    return __sha256_crypt_r (key, salt, (char *) data,
			     sizeof (struct crypt_data));

  /* Try to find out whether we have to use SHA512 encryption replacement.  */
  if (strncmp (sha512_salt_prefix, salt, sizeof (sha512_salt_prefix) - 1) == 0)
    return __sha512_crypt_r (key, salt, (char *) data,
			     sizeof (struct crypt_data));
#endif

  /*
   * Hack DES tables according to salt
   */
  if (!_ufc_setup_salt_r (salt, data))
    {
      __set_errno (EINVAL);
      return NULL;
    }

  /* FIPS rules out DES password encryption.  */
  // if (fips_enabled_p ())
  //   {
  //     __set_errno (EPERM);
  //     return NULL;
  //   }

  /*
   * Setup key schedule
   */
  _ufc_clearmem (ktab, (int) sizeof (ktab));
  (void) strncpy (ktab, key, 8);
  _ufc_mk_keytab_r (ktab, data);

  /*
   * Go for the 25 DES encryptions
   */
  _ufc_clearmem ((char*) res, (int) sizeof (res));
  _ufc_doit_r (xx,  data, &res[0]);

  /*
   * Do final permutations
   */
  _ufc_dofinalperm_r (res, data);

  /*
   * And convert back to 6 bit ASCII
   */
  _ufc_output_conversion_r (res[0], res[1], salt, data);

  /*
   * Erase key-dependent intermediate data.  Data dependent only on
   * the salt is not considered sensitive.
   */
  explicit_bzero (ktab, sizeof (ktab));
  explicit_bzero (data->keysched, sizeof (data->keysched));
  explicit_bzero (res, sizeof (res));

  return data->crypt_3_buf;
}