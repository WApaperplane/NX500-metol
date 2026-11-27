#ifndef EYE_PROC_IDENTITY_H
#define EYE_PROC_IDENTITY_H
struct eye_identity { unsigned long long starttime; char exe[4096]; char boot_id[128]; };
int eye_read_identity(const char *root, long pid, struct eye_identity *out);
#endif
