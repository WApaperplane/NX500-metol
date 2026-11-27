#ifndef EYE_FILE_IDENTITY_H
#define EYE_FILE_IDENTITY_H
#include <sys/stat.h>
int eye_verify_file(const char *, const char *, const char *, struct stat *);
int eye_mapping_matches(const char *, long, const char *, const struct stat *);
#endif
