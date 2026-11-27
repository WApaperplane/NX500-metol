#ifndef EYE_PROOF_H
#define EYE_PROOF_H
struct eye_proof {
    char generation[129], image_generation[129], boot_id[129];
    char pid[129], starttime[129], build_id[129], report_id[129];
};
int eye_read_proof(const char *filename, struct eye_proof *out);
#endif
