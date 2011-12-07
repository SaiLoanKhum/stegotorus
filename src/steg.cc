/* Copyright 2011 Zack Weinberg
   See LICENSE for other credits and copying information
*/

#include "util.h"
#include "steg.h"

/* Report whether a named steg-module is supported. */

int
steg_is_supported(const char *name)
{
  const steg_module *const *s;
  for (s = supported_stegs; *s; s++)
    if (!strcmp(name, (**s).name))
      return 1;
  return 0;
}

/* Instantiate a steg module by name. */
steg_t *
steg_new(const char *name)
{
  const steg_module *const *s;
  for (s = supported_stegs; *s; s++)
    if (!strcmp(name, (**s).name))
      return (**s).new_(/*is_clientside=*/true);
  return NULL;
}

/* Instantiate a steg module by detection. */
steg_t *
steg_detect(conn_t *conn)
{
  const steg_module *const *s;
  for (s = supported_stegs; *s; s++)
    if ((**s).detect(conn))
      return (**s).new_(/*is_clientside=*/false);
  return NULL;
}

/* Define this here rather than in the class definition so that the
   vtable will be emitted in only one place. */
steg_t::~steg_t() {}