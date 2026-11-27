#ifndef EYE_TOKEN_H
#define EYE_TOKEN_H
typedef int (*eye_validate_callback)(void *context);
int eye_publish_token(const char *root, const char *pid, const char *starttime,
                      const char *generation, eye_validate_callback validate, void *context);
#endif
